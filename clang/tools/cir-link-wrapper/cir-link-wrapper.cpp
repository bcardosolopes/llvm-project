//===- cir-link-wrapper.cpp - link-time CIR for fat objects ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Runs the CIR link-time pipeline in the middle of an ordinary link.
//
// The build system does not learn anything new: it keeps invoking the clang
// driver to link, exactly as before, and the driver swaps this in for the
// linker. That is the same trick clang-linker-wrapper plays for offloading --
// it is handed `--linker-path=<real linker>` plus the whole original command
// line, does its work, and then runs the real linker.
//
// What it does with that:
//
//   1. partition the inputs into objects carrying a .cir section and
//      everything else, looking inside static archives for members that
//      carry one when --cir-merge-archives is given;
//   2. hand the CIR-bearing objects to cir-link, which merges them into one
//      module and lowers it to LLVM IR in the same process (it reads the
//      sections itself, so there is no per-object extraction step for loose
//      inputs -- at a real library that would be hundreds; archive members
//      are the exception, and are extracted to the scratch directory);
//   3. hand the merged bitcode to the real linker, which runs its own LTO
//      over it (one file, or one per piece with --cir-partitions=<N>, where
//      cir-link lowers the merged module as N pieces in parallel);
//   4. run the real linker with that bitcode in place of the objects it
//      came from.
//
// If no input carries CIR it execs the linker untouched, so putting this on a
// link that has no fat objects changes nothing -- though it is not quite free:
// every argument that looks like an object is opened to find out. That is what
// makes it safe to enable per library while the rest of an app links normally.
//
// KNOWN LIMITATIONS, both of the same kind -- an input can carry CIR and still
// be passed through unmerged, so whether a translation unit participates
// depends on how it arrives:
//
// Only arguments named `*.o` are probed, because a value belonging to a linker
// option this tool does not know about must not be mistaken for an input. A
// fat object named anything else is passed through.
//
// Archive members are merged only with --cir-merge-archives, and then only
// from archives named directly as `*.a` -- `-lfoo` is never resolved --
// outside --whole-archive and --start-lib/--end-lib. Under --whole-archive
// every member links unconditionally, so absorbing members would duplicate
// every definition; inside a lazy group the explicit only-if-needed request
// wins over coverage, the same rule loose objects follow. The archive itself
// stays on the command line, so members without CIR keep lazy archive
// semantics and the linker pulls them exactly as before; it never pulls a
// merged member, because the merged bitcode already defines its symbols and
// a lazy archive only provides what is still undefined.
//
// Merging a member links it eagerly, which is the one semantic change:
// a member nothing needs is now present anyway, code and static
// initializers included. Two archives defining the same strong symbol are
// unified by cir-link instead of first-wins; for ODR-identical definitions
// that is the same answer, and for genuinely divergent ones the two types
// now coexist where the old link silently kept one.
//
// When the merged link fails after absorbing archive members, the wrapper
// retries the exact original command line unmerged (with a warning) rather
// than failing the build: the eager member is what broke lazy resolution,
// and the unmerged link resolves lazily again. Loose-only merges never
// retry -- without absorbed members there is no laziness change, so the
// failure is real and stays loud.
//
// The list of options whose value is a separate token is hand-maintained. An
// option missing from it whose value ends in `.o` and carries CIR would be
// taken for an input, leaving the linker a dangling option; the `.o` check
// narrows that but does not close it.
//
// And in the other direction, which is the one that changes meaning rather
// than coverage: an absorbed object is removed from the link and replaced by
// what the merged CIR module lowers to, so anything in it that CIR does not
// represent -- file-scope inline asm, hand-rolled sections, symbols produced
// outside the CIR pipeline -- is gone. The link still succeeds; it just means
// something different.
//
// Absorbed objects also all move to the position of the *first* of them.
// Order matters to a linker, so a fat object that appeared after an archive
// is now resolved before it.
//
// Deliberately a separate tool rather than driver-internal jobs: a relink
// driven through clang with `--ld-path=` pointing at a trampoline already has
// the seam, so this slots into it without the build system needing a new
// action.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/Object/Archive.h"
#include "llvm/Object/Binary.h"
#include "llvm/Object/ELFObjectFile.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/FileUtilities.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Program.h"
#include "llvm/Support/StringSaver.h"
#include "llvm/Support/raw_ostream.h"

#include <optional>
#include <string>
#include <vector>

using namespace llvm;

namespace {

/// Set from --cir-wrapper-verbose; read by run().
bool verbose = false;

/// Does this object carry CIR?
bool carriesCIRSection(object::ObjectFile *obj) {
  for (const object::SectionRef &sec : obj->sections()) {
    Expected<StringRef> name = sec.getName();
    if (!name) {
      consumeError(name.takeError());
      continue;
    }
    if (*name == ".cir")
      return true;
  }
  return false;
}

/// Does this file look like an object carrying CIR?
///
/// Anything that is not an object -- an archive, a linker script, a plain
/// flag -- is not our business and is passed through untouched.
bool carriesCIR(StringRef path) {
  auto binary = object::createBinary(path);
  if (!binary) {
    consumeError(binary.takeError());
    return false;
  }
  auto *obj = dyn_cast<object::ObjectFile>(binary->getBinary());
  if (!obj)
    return false;
  return carriesCIRSection(obj);
}

/// Indices of the members of an archive that carry CIR, or std::nullopt
/// when the path is not an archive at all -- a linker script named `.a`
/// is legal, and the linker will diagnose what it cannot use.
///
/// A member that cannot be read is skipped with a warning, which is safe
/// rather than merely lenient: a skipped member is never merged, so the
/// merged bitcode cannot define its symbols, so the linker still pulls it
/// from the archive exactly as before.
std::optional<std::vector<unsigned>> cirMembersOf(StringRef path) {
  auto binary = object::createBinary(path);
  if (!binary) {
    consumeError(binary.takeError());
    return std::nullopt;
  }
  auto *archive = dyn_cast<object::Archive>(binary->getBinary());
  if (!archive)
    return std::nullopt;
  std::vector<unsigned> members;
  unsigned index = 0;
  Error err = Error::success();
  for (const object::Archive::Child &child :
       llvm::make_range(archive->child_begin(err), archive->child_end())) {
    unsigned current = index++;
    Expected<StringRef> bytes = child.getBuffer();
    if (!bytes) {
      errs() << "cir-link-wrapper: cannot read a member of '" << path
             << "': " << toString(bytes.takeError()) << "; leaving it\n";
      continue;
    }
    MemoryBufferRef memberRef(*bytes, path);
    auto member = object::createBinary(memberRef);
    if (!member) {
      consumeError(member.takeError());
      continue;
    }
    auto *obj = dyn_cast<object::ObjectFile>(member->get());
    if (obj && carriesCIRSection(obj))
      members.push_back(current);
  }
  if (err) {
    errs() << "cir-link-wrapper: cannot list '" << path
           << "': " << toString(std::move(err)) << "; leaving it\n";
    return std::nullopt;
  }
  return members;
}

/// A sibling tool, looked up next to this binary so a build tree and an
/// installed toolchain both work without configuration.
std::string siblingTool(const char *argv0, StringRef name) {
  SmallString<128> dir(sys::fs::getMainExecutable(argv0, (void *)&carriesCIR));
  sys::path::remove_filename(dir);
  SmallString<128> candidate = dir;
  sys::path::append(candidate, name);
  if (sys::fs::can_execute(candidate))
    return std::string(candidate);
  if (ErrorOr<std::string> found = sys::findProgramByName(name))
    return *found;
  // Naming it here beats the generic "could not execute" the exec would
  // otherwise produce, which does not say which of the three tools is absent.
  errs() << "cir-link-wrapper: cannot find '" << name << "' beside " << argv0
         << " or on PATH\n";
  return std::string();
}

/// Run a subprocess, moving the arguments into a response file if the command
/// line is too long for the system.
///
/// Every invocation here can overflow, not just the final link: a real library
/// contributes hundreds of CIR objects, and they are all named on cir-link's
/// command line.
int run(ArrayRef<StringRef> argv) {
  if (verbose) {
    for (StringRef a : argv)
      errs() << a << ' ';
    errs() << '\n';
  }

  SmallVector<StringRef, 4> viaFile;
  SmallString<128> rspPath;
  std::string rspArg; // outlives viaFile, which points into it
  FileRemover remover;
  if (!sys::commandLineFitsWithinSystemLimits(argv.front(),
                                              argv.drop_front())) {
    if (std::error_code ec =
            sys::fs::createTemporaryFile("cir-link-wrapper", "rsp", rspPath)) {
      errs() << "cir-link-wrapper: cannot create a response file: "
             << ec.message() << '\n';
      return 1;
    }
    remover.setFile(rspPath, /*DeleteIt=*/!verbose);
    std::error_code ec;
    raw_fd_ostream rsp(rspPath, ec, sys::fs::OF_Text);
    if (ec) {
      errs() << "cir-link-wrapper: cannot write '" << rspPath
             << "': " << ec.message() << '\n';
      return 1;
    }
    for (StringRef a : argv.drop_front()) {
      sys::printArg(rsp, a, /*Quote=*/true);
      rsp << '\n';
    }
    rsp.close();
    if (rsp.has_error()) {
      errs() << "cir-link-wrapper: cannot write '" << rspPath
             << "': " << rsp.error().message() << '\n';
      return 1;
    }
    rspArg = ("@" + rspPath).str();
    viaFile.assign({argv.front(), StringRef(rspArg)});
    argv = viaFile;
  }

  std::string err;
  int rc = sys::ExecuteAndWait(argv[0], argv, std::nullopt, {}, 0, 0, &err);
  if (rc != 0 && !err.empty())
    errs() << "cir-link-wrapper: " << err << '\n';
  // ExecuteAndWait reports -1 for "could not run it" and -2 for a crash or
  // timeout. Returned as-is those reach the shell as 255 and 254, which are
  // ordinary exit codes a real tool might produce; collapse them so a caller
  // can tell the pipeline failed rather than the subprocess having an opinion.
  return rc < 0 ? 1 : rc;
}

} // namespace

/// A cross-library optimizer's single-library link lists the inputs it took as
/// bitcode in `path`, and the codegen link after the cross-library step leaves
/// exactly those out, linking the library's code from the optimizer's object
/// instead. Here the linker took only the merged pieces, temporary files whose
/// names mean nothing to that later link, so the objects merged into them would
/// be linked again next to the optimizer's object: every symbol they define
/// duplicated. So the pieces' lines
/// give way to the loose objects they absorbed. Archive members are not
/// listed: their archive stays on the link for the members that carry no CIR,
/// and the linker pulls no merged member from it.
///
/// A piece is recognised by its path as passed or as the same file under
/// another spelling. If the linker listed none of them, the objects are
/// appended, with a warning: they are constituents either way, and leaving
/// them out brings back the duplicates with nothing pointing at the cause. A
/// link that wrote no file has nothing to rewrite. The new list replaces the
/// old one by rename, so a failed write leaves the linker's file whole -- and
/// fails the link, since the codegen link would read the wrong list.
static bool rewriteConstituents(StringRef path,
                                ArrayRef<std::string> mergedObjs,
                                ArrayRef<StringRef> absorbed) {
  if (!sys::fs::exists(path)) {
    errs() << "cir-link-wrapper: warning: the linker wrote no '" << path
           << "'; nothing to rewrite\n";
    return true;
  }
  auto buffer = MemoryBuffer::getFile(path, /*IsText=*/true);
  if (!buffer) {
    errs() << "cir-link-wrapper: cannot read '" << path
           << "': " << buffer.getError().message() << '\n';
    return false;
  }
  auto isPiece = [&](StringRef line) {
    return llvm::any_of(mergedObjs, [&](const std::string &piece) {
      return line == piece || sys::fs::equivalent(line, piece);
    });
  };
  SmallVector<StringRef> lines;
  (*buffer)->getBuffer().split(lines, '\n', /*MaxSplit=*/-1,
                               /*KeepEmpty=*/false);
  std::string out;
  bool replaced = false;
  for (StringRef line : lines) {
    line = line.rtrim();
    if (line.empty())
      continue;
    if (!isPiece(line)) {
      out += line;
      out += '\n';
      continue;
    }
    if (replaced)
      continue;
    replaced = true;
    for (StringRef object : absorbed) {
      out += object;
      out += '\n';
    }
  }
  if (!replaced && !absorbed.empty()) {
    errs() << "cir-link-wrapper: warning: '" << path
           << "' lists none of the merged pieces; appending the "
           << absorbed.size() << " objects merged into them\n";
    for (StringRef object : absorbed) {
      out += object;
      out += '\n';
    }
  }
  Expected<sys::fs::TempFile> temp = sys::fs::TempFile::create(
      path + "-%%%%%%", sys::fs::all_read | sys::fs::owner_write);
  if (!temp) {
    errs() << "cir-link-wrapper: cannot write next to '" << path
           << "': " << toString(temp.takeError()) << '\n';
    return false;
  }
  {
    raw_fd_ostream file(temp->FD, /*shouldClose=*/false);
    file << out;
    file.flush();
    if (file.has_error()) {
      std::error_code ec = file.error();
      file.clear_error();
      consumeError(temp->discard());
      errs() << "cir-link-wrapper: cannot write '" << path
             << "': " << ec.message() << '\n';
      return false;
    }
  }
  if (Error err = temp->keep(path)) {
    errs() << "cir-link-wrapper: cannot replace '" << path
           << "': " << toString(std::move(err)) << '\n';
    return false;
  }
  return true;
}

/// Write the data symbols the shared libraries on the link line define, one
/// per line, for cir-link: a linkonce global other inputs declare is deferred
/// to its owner only if the owner is there to bind to (see cir-link's
/// deferDiscardableDataToOwners). Exported data only -- typeinfo, vtables --
/// since only globals are deferred. A file that is not an ELF shared library,
/// such as a linker script named libc.so, is skipped.
static bool writeSharedLibrarySymbols(ArrayRef<StringRef> libs,
                                      StringRef path) {
  std::error_code ec;
  raw_fd_ostream os(path, ec);
  if (ec) {
    errs() << "cir-link-wrapper: cannot write '" << path
           << "': " << ec.message() << '\n';
    return false;
  }
  for (StringRef lib : libs) {
    Expected<object::OwningBinary<object::Binary>> binary =
        object::createBinary(lib);
    if (!binary) {
      consumeError(binary.takeError());
      continue;
    }
    auto *elf = dyn_cast<object::ELFObjectFileBase>(binary->getBinary());
    if (!elf)
      continue;
    for (const object::ELFSymbolRef &sym : elf->getDynamicSymbolIterators()) {
      if (sym.getELFType() != ELF::STT_OBJECT)
        continue;
      Expected<uint32_t> flags = sym.getFlags();
      Expected<StringRef> name = sym.getName();
      if (!flags || !name) {
        consumeError(flags.takeError());
        consumeError(name.takeError());
        continue;
      }
      if ((*flags & object::SymbolRef::SF_Undefined) ||
          !(*flags & object::SymbolRef::SF_Global))
        continue;
      os << *name << '\n';
    }
  }
  return true;
}

int main(int argc, char **argv) {
  InitLLVM x(argc, argv);

  // argv is parsed by hand rather than with cl::opt.
  //
  // Almost everything here is a linker command line -- hundreds of flags this
  // tool has no opinion about and must pass through byte for byte. A cl::opt
  // table would have to know every one of them to avoid rejecting it, and
  // cl::ConsumeAfter cannot express "one leading option then an arbitrary
  // command line". The --linker-path= and --cir-* options are ours; the rest
  // is forwarded untouched.
  // The driver hands a link command over in a response file, so @file has to
  // be expanded before anything is read -- otherwise the whole command line,
  // --linker-path= included, is one unexamined argument.
  SmallVector<const char *, 256> args(argv, argv + argc);
  BumpPtrAllocator alloc;
  StringSaver saver(alloc);
  cl::ExpandResponseFiles(saver, cl::TokenizeGNUCommandLine, args);

  std::string linkerPath;
  std::string cirTriple;
  // A build system that gives every binary its own output directory has no
  // siblings to find, so there these are the normal case rather than an
  // override.
  std::string cirLinkPath;
  // Only used by --cir-split-lowering; the default path lowers in cir-link.
  std::string cirTranslatePath;
  bool splitLowering = false;
  // Off by default: merging archive members links them eagerly (see the
  // header comment), which is a semantic change a link has to ask for.
  bool mergeArchives = false;
  // Set by --cir-lto=<full|thin> and --cir-split-lto-unit, which the build
  // passes because only the build knows how it configured this link. The link
  // line itself carries no -flto for the wrapper to read.
  std::string ltoKind;
  bool splitLTOUnit = false;
  bool runInliner = false;
  std::string inlineThreshold;
  // --cir-partitions=<N>: cir-link lowers the merged module as N pieces in
  // parallel and writes N bitcode files, all of which go to the linker.
  unsigned partitions = 1;
  // StringRef, not std::string: after cl::ExpandResponseFiles every token is
  // owned either by argv or by the StringSaver below, both of which outlive
  // main, so copying thousands of them per link buys nothing.
  std::vector<StringRef> rest;
  rest.reserve(args.size());
  for (size_t i = 1; i < args.size(); ++i) {
    StringRef a(args[i]);
    if (a.consume_front("--linker-path="))
      linkerPath = a.str();
    else if (a.consume_front("--cir-triple="))
      cirTriple = a.str();
    else if (a.consume_front("--cir-translate-path="))
      cirTranslatePath = a.str();
    else if (a.consume_front("--cir-lto=")) {
      // Rejected rather than passed on: an unknown kind reaching cir-link
      // fails there anyway, but with a message about cir-link's options
      // instead of about what the build asked for.
      if (a != "full" && a != "thin") {
        errs() << "cir-link-wrapper: --cir-lto='" << a
               << "': expected full or thin\n";
        return 1;
      }
      ltoKind = a.str();
    } else if (a == "--cir-split-lto-unit")
      splitLTOUnit = true;
    else if (a == "--cir-inline")
      runInliner = true;
    else if (a.consume_front("--cir-inline-threshold="))
      inlineThreshold = a.str();
    else if (a == "--cir-split-lowering")
      splitLowering = true;
    else if (a.consume_front("--cir-partitions=")) {
      if (a.getAsInteger(10, partitions) || partitions == 0) {
        errs() << "cir-link-wrapper: --cir-partitions='" << a
               << "': expected a positive integer\n";
        return 1;
      }
    }
    else if (a == "--cir-merge-archives")
      mergeArchives = true;
    else if (a.consume_front("--cir-link-path="))
      cirLinkPath = a.str();
    else if (a == "--cir-wrapper-verbose")
      verbose = true;
    else
      rest.push_back(a);
  }
  if (linkerPath.empty()) {
    errs() << "cir-link-wrapper: --linker-path= is required\n";
    return 1;
  }

  // Linker options whose value is a separate token. The value is a path, not
  // an input, and probing it is not harmless: on a relink whose output happens
  // to be a fat object left by an earlier build, `-o out.o` would move the
  // output path into the CIR set and leave the linker a dangling `-o`.
  static constexpr StringRef separateValueOptions[] = {"-o",
                                                       "--output",
                                                       "-T",
                                                       "--script",
                                                       "-e",
                                                       "--entry",
                                                       "-u",
                                                       "--undefined",
                                                       "-y",
                                                       "--trace-symbol",
                                                       "-z",
                                                       "-m",
                                                       "-L",
                                                       "--library-path",
                                                       "-l",
                                                       "--library",
                                                       "-F",
                                                       "-filter",
                                                       "--filter",
                                                       "-R",
                                                       "-Y",
                                                       "-Map",
                                                       "--Map",
                                                       "-plugin",
                                                       "-plugin-opt",
                                                       "--defsym",
                                                       "--wrap",
                                                       "--soname",
                                                       "-soname",
                                                       "--rpath",
                                                       "-rpath",
                                                       "--sysroot",
                                                       "--out-implib",
                                                       "--dynamic-list",
                                                       "--just-symbols",
                                                       "--exclude-libs",
                                                       "--version-script",
                                                       "-dynamic-linker",
                                                       "--dynamic-linker",
                                                       "--retain-symbols-file"};

  // Partition. Order is preserved for everything that is passed through,
  // because a link is order-sensitive.
  std::vector<StringRef> cirInputs;
  cirInputs.reserve(rest.size());
  // An archive that contributes members. The merged bitcode goes where
  // the first CIR input sat, and a member's position is its archive's,
  // which mergedSlot already records.
  struct ArchiveContrib {
    StringRef path;
    std::vector<unsigned> members;
  };
  std::vector<ArchiveContrib> archiveContribs;
  // Archives repeat on real command lines (libA libB libA for cycles);
  // nullopt means not yet probed. Each unique path is probed once and each
  // member merged once.
  StringMap<std::optional<std::vector<unsigned>>> archiveCache;
  std::vector<StringRef> linkerArgs;
  linkerArgs.reserve(rest.size());
  // Where the merged object goes: the slot the first CIR input occupied.
  // Appending it instead would place it after the -l libraries, --end-group
  // and crtend/crtn, which changes what its undefined symbols can resolve
  // against and where its init/fini entries land.
  std::optional<size_t> mergedSlot;
  // Objects between --start-lib and --end-lib have archive-member semantics:
  // the linker takes them only if something needs them. Absorbing one into the
  // merged object would link it unconditionally, which pulls in definitions
  // the link deliberately left out and can turn into a duplicate symbol. They
  // are passed through instead.
  unsigned lazyGroupDepth = 0;
  // Inside --whole-archive every member links unconditionally, so absorbing
  // members would duplicate every definition. lld accepts both spellings.
  bool inWholeArchive = false;
  // After a bare "--" a GNU-style linker treats every remaining token as a
  // file name, including one that starts with '-'. Without this such a name
  // would be read as an option, and if it collided with a separate-value
  // option the input after it would be swallowed as its value.
  bool endOfOptions = false;
  // The shared libraries the link names, for writeSharedLibrarySymbols.
  std::vector<StringRef> sharedLibs;
  // Where a cross-library optimizer's single-library link lists its bitcode
  // inputs; see rewriteConstituents.
  StringRef constituentsFile;
  for (size_t i = 0; i < rest.size(); ++i) {
    StringRef a(rest[i]);
    if (!endOfOptions && a == "--") {
      endOfOptions = true;
      linkerArgs.push_back(rest[i]);
      continue;
    }
    if (!endOfOptions && !a.empty() && a[0] == '-') {
      // The optimizer's constituents file, in any of the spellings lld
      // accepts; the joined form leaves `a` itself alone for the checks
      // below.
      if (a == "--constituents-file" || a == "-constituents-file") {
        linkerArgs.push_back(rest[i]);
        if (i + 1 < rest.size()) {
          constituentsFile = rest[i + 1];
          linkerArgs.push_back(rest[++i]);
        }
        continue;
      }
      for (StringRef prefix : {"--constituents-file=", "-constituents-file="})
        if (a.starts_with(prefix))
          constituentsFile = a.drop_front(prefix.size());
      // lld accepts both spellings of these.
      if (a == "--start-lib" || a == "-start-lib")
        ++lazyGroupDepth;
      else if ((a == "--end-lib" || a == "-end-lib") && lazyGroupDepth)
        --lazyGroupDepth;
      else if (a == "--whole-archive" || a == "-whole-archive")
        inWholeArchive = true;
      else if (a == "--no-whole-archive" || a == "-no-whole-archive")
        inWholeArchive = false;
      linkerArgs.push_back(rest[i]);
      if (llvm::is_contained(separateValueOptions, a) && i + 1 < rest.size())
        linkerArgs.push_back(rest[++i]);
      continue;
    }
    if (lazyGroupDepth) {
      linkerArgs.push_back(rest[i]);
      continue;
    }
    // The suffix check is the backstop for options this list does not know
    // about: a fat object is always a .o here, so a linker script, map file,
    // implib or sysroot handed to an unrecognised option cannot be mistaken
    // for an input even if it happens to be a file we could open.
    if (a.ends_with(".o") && carriesCIR(a)) {
      if (!mergedSlot)
        mergedSlot = linkerArgs.size();
      cirInputs.push_back(rest[i]);
      continue;
    }
    // An archive contributes its CIR-carrying members and stays on the line
    // for the rest: the linker never pulls a merged member, because the
    // merged bitcode already defines its symbols. Inside a lazy group the
    // explicit only-if-needed request wins over coverage, the same rule
    // loose objects follow.
    if (mergeArchives && a.ends_with(".a") && !lazyGroupDepth &&
        !inWholeArchive) {
      auto cached = archiveCache.find(a);
      if (cached == archiveCache.end())
        cached = archiveCache
                     .try_emplace(
                         a, cirMembersOf(a).value_or(std::vector<unsigned>()))
                     .first;
      const std::vector<unsigned> &members = *cached->second;
      if (!members.empty() &&
          llvm::none_of(archiveContribs,
                        [&](const ArchiveContrib &c) { return c.path == a; })) {
        if (!mergedSlot)
          mergedSlot = linkerArgs.size();
        archiveContribs.push_back({a, members});
      }
    }
    // A cheap filter on the name, so the thousands of objects on a link are
    // not opened; writeSharedLibrarySymbols skips any match that is not an
    // ELF shared library (a backup file, a linker script named libc.so).
    if ((a.ends_with(".so") || a.contains(".so.")) &&
        sys::fs::is_regular_file(a))
      sharedLibs.push_back(a);
    linkerArgs.push_back(rest[i]);
  }

  auto execLinker = [&](ArrayRef<std::string> mergedObjs) -> int {
    std::vector<StringRef> cmd;
    cmd.reserve(linkerArgs.size() + mergedObjs.size() + 1);
    cmd.push_back(linkerPath);
    for (size_t i = 0; i < linkerArgs.size(); ++i) {
      if (mergedSlot == i)
        cmd.insert(cmd.end(), mergedObjs.begin(), mergedObjs.end());
      cmd.push_back(linkerArgs[i]);
    }
    // Every CIR input sat at the end, so the slot is one past the last
    // argument and the merged objects are appended.
    if (mergedSlot == linkerArgs.size())
      cmd.insert(cmd.end(), mergedObjs.begin(), mergedObjs.end());

    return run(cmd);
  };

  // Nothing to do. Not an error: a link with no fat objects is the normal
  // case for most of an app, and this has to be transparent there.
  if (cirInputs.empty() && archiveContribs.empty())
    return execLinker({});

  // Rooted in the temp directory explicitly: createUniqueDirectory takes a
  // path *prefix*, so a bare name would put the scratch directory in whatever
  // the link's working directory happens to be -- the build output tree, and
  // a hard failure when that directory is read-only.
  SmallString<128> tmpDir;
  sys::path::system_temp_directory(/*erasedOnReboot=*/true, tmpDir);
  sys::path::append(tmpDir, "cir-link-wrapper");
  if (std::error_code ec = sys::fs::createUniqueDirectory(tmpDir, tmpDir)) {
    errs() << "cir-link-wrapper: cannot create temporary directory: "
           << ec.message() << '\n';
    return 1;
  }
  // The intermediates are worth keeping when someone is debugging the
  // pipeline and are pure litter otherwise, so --cir-wrapper-verbose keeps
  // them and every other exit path removes them.
  struct ScratchDir {
    SmallString<128> path;
    bool keep;
    ~ScratchDir() {
      if (!keep)
        sys::fs::remove_directories(path);
      else
        errs() << "cir-link-wrapper: intermediates kept in '" << path << "'\n";
    }
  } scratch{tmpDir, verbose};

  auto tmp = [&](StringRef name) {
    SmallString<128> p = tmpDir;
    sys::path::append(p, name);
    return std::string(p);
  };

  const std::string mergedCIR = tmp("merged.cir");
  const std::string mergedBC = tmp("merged.bc");
  const std::string sharedSymbols = tmp("shared-library-symbols.txt");
  if (!writeSharedLibrarySymbols(sharedLibs, sharedSymbols))
    return 1;
  const std::string sharedSymbolsArg =
      "--shared-library-symbols=" + sharedSymbols;

  // Extract the contributing archive members. cir-link reads object files,
  // not archives, and member names are unusable as paths -- thin archives
  // carry slashes, and nothing stops two members sharing a basename -- so
  // each lands in its own file, numbered in command-line order. A member
  // extract is skipped with a warning: it is never merged, so the linker
  // still pulls it from the archive exactly as before.
  std::vector<std::string> extractedPaths;
  unsigned memberCounter = 0;
  for (const ArchiveContrib &contrib : archiveContribs) {
    auto binary = object::createBinary(contrib.path);
    if (!binary) {
      errs() << "cir-link-wrapper: cannot reopen '" << contrib.path
             << "': " << toString(binary.takeError()) << "; leaving it\n";
      continue;
    }
    object::Archive *archive = dyn_cast<object::Archive>(binary->getBinary());
    if (!archive) {
      errs() << "cir-link-wrapper: '" << contrib.path
             << "' is no longer an archive; leaving it\n";
      continue;
    }
    Error err = Error::success();
    unsigned index = 0;
    for (const object::Archive::Child &child :
         llvm::make_range(archive->child_begin(err), archive->child_end())) {
      if (!llvm::is_contained(contrib.members, index++))
        continue;
      Expected<StringRef> bytes = child.getBuffer();
      if (!bytes) {
        errs() << "cir-link-wrapper: cannot read a member of '" << contrib.path
               << "': " << toString(bytes.takeError()) << "; leaving it\n";
        continue;
      }
      std::string memberFile =
          tmp("member-" + std::to_string(memberCounter++) + ".o");
      std::error_code ec;
      raw_fd_ostream out(memberFile, ec, sys::fs::OF_None);
      if (ec) {
        errs() << "cir-link-wrapper: cannot write '" << memberFile
               << "': " << ec.message() << "; leaving it\n";
        sys::fs::remove(memberFile);
        continue;
      }
      out << *bytes;
      out.close();
      if (out.has_error()) {
        errs() << "cir-link-wrapper: cannot write '" << memberFile
               << "': " << out.error().message() << "; leaving it\n";
        sys::fs::remove(memberFile);
        continue;
      }
      extractedPaths.push_back(std::string(memberFile));
    }
    if (err)
      errs() << "cir-link-wrapper: cannot list '" << contrib.path
             << "': " << toString(std::move(err)) << "; leaving it\n";
  }
  // The loose objects come first; the members after them have no path of
  // their own outside this link.
  const size_t looseInputs = cirInputs.size();
  for (const std::string &path : extractedPaths)
    cirInputs.push_back(path);

  // 1. Merge. cir-link reads the .cir section out of each object itself.
  {
    // The string has to outlive the StringRef vector; constructing the vector
    // straight from the call would leave cmd[0] pointing at a temporary.
    std::string tool =
        cirLinkPath.empty() ? siblingTool(argv[0], "cir-link") : cirLinkPath;
    if (tool.empty())
      return 1;
    std::string tripleArg = cirTriple;

    // How to lower the merged module and write it, built once and appended
    // to whichever tool ends up doing the lowering. Two copies of this list is
    // how the --cir-split-lowering path lost it and went on writing
    // summary-less bitcode that lld rejects.
    std::string ltoArg = ltoKind.empty() ? "" : "--lto=" + ltoKind;
    std::string inlineThresholdArg =
        inlineThreshold.empty() ? ""
                                : "--cir-inline-threshold=" + inlineThreshold;
    auto appendLTOArgs = [&](std::vector<StringRef> &cmd) {
      if (!ltoArg.empty())
        cmd.push_back(ltoArg);
      if (splitLTOUnit)
        cmd.push_back("--split-lto-unit");
      if (runInliner)
        cmd.push_back("--inline");
      if (!inlineThresholdArg.empty())
        cmd.push_back(inlineThresholdArg);
    };

    // Merge and lower in one process. cir-link holds the merged module in
    // memory already, so the old shape -- write it out, start another tool,
    // parse it back -- paid for a round trip that carried nothing.
    //
    // --cir-split-lowering runs the two halves as two processes instead:
    // cir-link merges, cir-translate lowers. It costs the merged module being
    // written and read once more, and what it buys is a lower peak. Merging and
    // lowering each need roughly what the larger of them needs, but in one
    // process they need the sum at the moment lowering runs, and a build
    // machine with a per-action memory limit cares about the peak and not the
    // total work. Measured on one real library: 5.66 GB in one process against
    // 3.12 GB and 3.73 GB in two.
    //
    // The lowering half is cir-translate rather than a second cir-link,
    // because cir-link would put its single input through the merge
    // architecture's private-context-and-bytecode round trip to no purpose --
    // that costs about 0.6 GB and 17s over the tool that just parses the file
    // once. Both tools call the same lowerCIRModuleToLLVMIR, so the split
    // cannot make the answer differ.
    if (splitLowering) {
      // cir-translate lowers in one piece.
      if (partitions > 1) {
        errs() << "cir-link-wrapper: --cir-partitions is not supported with "
                  "--cir-split-lowering\n";
        return 1;
      }
      std::vector<StringRef> mergeCmd;
      mergeCmd.reserve(cirInputs.size() + 3);
      mergeCmd.push_back(tool);
      mergeCmd.push_back(sharedSymbolsArg);
      for (StringRef o : cirInputs)
        mergeCmd.push_back(o);
      mergeCmd.push_back("-o");
      mergeCmd.push_back(mergedCIR);
      if (int rc = run(mergeCmd)) {
        errs() << "cir-link-wrapper: cir-link failed on " << cirInputs.size()
               << " objects\n";
        return rc;
      }

      std::string translateTool = cirTranslatePath.empty()
                                      ? siblingTool(argv[0], "cir-translate")
                                      : cirTranslatePath;
      if (translateTool.empty())
        return 1;
      // Bitcode, not text: the consumer is clang. cir-translate cannot see
      // what format its own input was in, so the side that does know says so.
      std::vector<StringRef> lowerCmd{
          translateTool,         mergedCIR, "-cir-to-llvmir",
          "--emit-llvm-bitcode", "-o",      mergedBC};
      appendLTOArgs(lowerCmd);
      if (!tripleArg.empty()) {
        lowerCmd.push_back("--target");
        lowerCmd.push_back(tripleArg);
      }
      if (int rc = run(lowerCmd)) {
        errs() << "cir-link-wrapper: lowering the merged module failed\n";
        return rc;
      }
    } else {
      std::vector<StringRef> cmd;
      cmd.reserve(cirInputs.size() + 8);
      cmd.push_back(tool);
      cmd.push_back(sharedSymbolsArg);
      for (StringRef o : cirInputs)
        cmd.push_back(o);
      cmd.push_back("--emit-llvm");
      appendLTOArgs(cmd);
      std::string partitionsArg;
      if (partitions > 1) {
        partitionsArg = "--partitions=" + std::to_string(partitions);
        cmd.push_back(partitionsArg);
      }
      cmd.push_back("-o");
      cmd.push_back(mergedBC);
      if (!tripleArg.empty()) {
        cmd.push_back("--target");
        cmd.push_back(tripleArg);
      }
      // Only when someone is going to look at it. In the split path the
      // merged module is on disk anyway.
      std::string emitCIRArg;
      if (verbose) {
        emitCIRArg = "--emit-cir=" + mergedCIR;
        cmd.push_back(emitCIRArg);
      }
      if (int rc = run(cmd)) {
        errs() << "cir-link-wrapper: cir-link failed on " << cirInputs.size()
               << " objects\n";
        return rc;
      }
    }
  }

  // 2. Link, with the merged bitcode standing in for the objects it absorbed.
  //
  // Handed over as bitcode, not compiled first. The constituents this replaces
  // reached the linker as bitcode too, and the linker's LTO is what optimizes
  // them; the only difference here is that it gets one input where it used to
  // get hundreds, because the merging already happened a level down in CIR.
  //
  // Nothing in the CIR path runs an LLVM optimization pipeline. There is no
  // per-translation-unit LLVM stage to match -- each TU stopped at CIR -- so a
  // whole-module compile here would be an optimization the non-CIR build does
  // not perform at this point, on top of the LTO that follows it. It was one,
  // and on a 380-object library it cost ten minutes and 27 GB to do work the
  // linker then did again.
  // cir-link names piece 0 after -o and piece k after <-o>.k.
  std::vector<std::string> mergedObjs{mergedBC};
  for (unsigned piece = 1; piece < partitions; ++piece)
    mergedObjs.push_back(mergedBC + "." + std::to_string(piece));
  int rc = execLinker(mergedObjs);
  if (rc != 0) {
    if (archiveContribs.empty())
      return rc;
    // The merged link failed after absorbing archive members. Eager member
    // extraction breaks lazy archive semantics -- a member nothing needs is
    // now present anyway, uses and all -- so a link that resolved lazily can
    // die merged on an undefined symbol. Retry the exact original command
    // line unmerged: the loose objects go back as the plain LTO inputs they
    // were without CIR, and the archives, which never left the line, resolve
    // lazily again. Gated on absorbed archives on purpose: a loose-only
    // merge changes no laziness, so its link failures stay loud rather than
    // masking a mismerge as a slow link. Loud either way: the retry still
    // warns, so a fallback never silently replaces the merge.
    errs() << "cir-link-wrapper: warning: link with merged CIR objects failed"
              " (rc="
           << rc << "); retrying with the original unmerged inputs\n";
    std::vector<StringRef> origCmd;
    origCmd.reserve(rest.size() + 1);
    origCmd.push_back(linkerPath);
    origCmd.insert(origCmd.end(), rest.begin(), rest.end());
    return run(origCmd);
  }
  if (!constituentsFile.empty() &&
      !rewriteConstituents(constituentsFile, mergedObjs,
                           ArrayRef(cirInputs).take_front(looseInputs)))
    return 1;
  return 0;
}
