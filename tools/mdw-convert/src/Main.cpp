// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

// mdw-convert (Phase 8.11): turns .milk presets into .milkdawp presets with
// proposed Macros, written beside the originals (or under --out). A .milk is only ever read:
// never written, moved, renamed or deleted (exploration doc §5.1).
//
// Usage:
//   mdw-convert [options] <folder | preset.milk>...
//
//   --out <dir>              write the .milkdawp files under <dir>, in the same layout
//                            (default: beside each .milk)
//   --overwrite              replace .milkdawp files already there (default: keep them)
//   --dry-run                write no presets, only the report
//   --exclude <file>         presets to leave alone, one relative path per line
//   --prune-excluded         delete the .milkdawp beside an excluded preset (generated packs)
//   --report <file.tsv>      one line per preset: path, outcome, detail, Macros
//   --verify                 render each original and its conversion headless and
//                            leave out any that looks different
//   --frames <n>             frames per render for --verify (default 60)
//   --textures <dir>         texture folder for --verify (default: the bundled one)
//   --journal <file>         keep --verify results here; a rerun reuses them, so a
//                            run projectM crashed in carries on (that preset fails)
//   --write-exclusions <file>  the presets projectM can't load, as an --exclude file (other
//                            rejections are listed as comments)
//   --quiet                  no progress
//
// Dev aids:
//   mdw-convert --print-compiled <preset>   what projectM is given for it
//   mdw-convert --compare <a> <b>           how far apart two presets render, as --verify measures

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "Verifier.h"
#include "milkdawp/core/MilkConvert.h"
#include "milkdawp/core/Version.h"
#include "milkdawp/engine/BundledContent.h"

namespace {

using namespace milkdawp;

void printUsage() {
  std::cerr << "usage: mdw-convert [--out <dir>] [--overwrite] [--dry-run] [--exclude <file> [--prune-excluded]] [--report <file.tsv>]\n"
               "                   [--verify [--frames <n>] [--textures <dir>] [--journal <file>]\n"
               "                    [--write-exclusions <file>]] [--quiet] <folder | preset.milk>...\n";
}

std::optional<std::string> readFile(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return std::nullopt;
  }
  return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

std::string utf8(const std::filesystem::path& path) {
  const auto text = path.generic_u8string();
  return {text.begin(), text.end()};
}

// Verify results by preset path. "start" is written before each render and
// "done" after it, so a path with only "start" is where projectM crashed.
class Journal {
public:
  explicit Journal(std::filesystem::path path) : path_(std::move(path)) {
    if (const auto text = readFile(path_)) {
      std::istringstream in(*text);
      std::string line;
      std::set<std::string> started;
      while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') {
          line.pop_back();
        }
        const auto tab = line.find('\t');
        if (tab == std::string::npos) {
          continue;
        }
        const auto kind = line.substr(0, tab);
        auto rest = line.substr(tab + 1);
        if (kind == "start") {
          started.insert(rest);
        } else if (kind == "done") {
          const auto split = rest.find('\t');
          const auto key = rest.substr(0, split);
          auto reason = split == std::string::npos ? std::string{} : rest.substr(split + 1);
          results_[key] = reason.substr(0, reason.find('\t'));
          started.erase(key);
        }
      }
      for (const auto& key : started) {
        results_[key] = "projectM crashed on it (a run stopped here)";
      }
    }
    out_.open(path_, std::ios::binary | std::ios::app);
  }

  [[nodiscard]] std::optional<std::string> find(const std::string& key) const {
    const auto found = results_.find(key);
    return found == results_.end() ? std::nullopt : std::optional<std::string>(found->second);
  }
  void start(const std::string& key) { out_ << "start\t" << key << "\n" << std::flush; }
  // `how` (exact, or the two differences) is for reading the journal, not reused.
  void done(const std::string& key, const std::string& reason, const std::string& how) {
    out_ << "done\t" << key << "\t" << reason << "\t" << how << "\n" << std::flush;
  }

private:
  std::filesystem::path path_;
  std::map<std::string, std::string> results_;
  std::ofstream out_;
};

} // namespace

int main(int argc, char** argv) {
  core::ConvertOptions options;
  std::vector<std::filesystem::path> roots;
  std::vector<std::filesystem::path> excludeFiles;
  std::optional<std::filesystem::path> reportPath;
  std::optional<std::filesystem::path> journalPath;
  std::optional<std::filesystem::path> exclusionsOut;
  std::optional<std::string> textures;
  bool verify = false;
  bool quiet = false;
  int frames = 60;

  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    const auto value = [&]() -> std::optional<std::string> {
      if (i + 1 >= argc) {
        std::cerr << arg << " needs a value\n";
        return std::nullopt;
      }
      return std::string(argv[++i]);
    };
    if (arg == "--help" || arg == "-h") {
      printUsage();
      return 0;
    } else if (arg == "--print-compiled") {
      // What projectM would be given for one preset (.milk or .milkdawp), on stdout.
      const auto v = value();
      const auto text = v ? readFile(*v) : std::nullopt;
      if (!text) {
        std::cerr << "can't read the preset\n";
        return 1;
      }
      const auto parsed = core::parseMilkdawp(*text);
      const auto preset = parsed.preset.controls.empty() && parsed.preset.milk == *text
                              ? core::convertMilk(*text, utf8(std::filesystem::path(*v).filename()))
                              : parsed.preset;
      std::cout << core::compileForProjectM(preset);
      return 0;
    } else if (arg == "--compare") {
      // Dev aid: how far apart two preset texts render, as --verify measures it.
      const auto a = value();
      const auto b = value();
      const auto textA = a ? readFile(*a) : std::nullopt;
      const auto textB = b ? readFile(*b) : std::nullopt;
      if (!textA || !textB) {
        std::cerr << "--compare needs two readable presets\n";
        return 1;
      }
      mdw_convert::Verifier::Settings settings;
      if (const auto content = engine::BundledContent::find()) {
        settings.textureSearchPaths = content->textureSearchPaths();
      }
      std::string error;
      const auto verifier = mdw_convert::Verifier::create(settings, error);
      if (!verifier) {
        std::cerr << "can't render here: " << error << "\n";
        return 1;
      }
      for (const auto difference : verifier->compare(*textA, *textB)) {
        std::cout << difference << " ";
      }
      std::cout << "\n";
      return 0;
    } else if (arg == "--prune-excluded") {
      options.pruneExcluded = true;
    } else if (arg == "--overwrite") {
      options.overwrite = true;
    } else if (arg == "--dry-run") {
      options.dryRun = true;
    } else if (arg == "--verify") {
      verify = true;
    } else if (arg == "--quiet") {
      quiet = true;
    } else if (arg == "--out" || arg == "--exclude" || arg == "--report" || arg == "--journal" || arg == "--write-exclusions" ||
               arg == "--textures" || arg == "--frames") {
      const auto v = value();
      if (!v) {
        return 1;
      }
      if (arg == "--out") options.outputRoot = *v;
      if (arg == "--exclude") excludeFiles.emplace_back(*v);
      if (arg == "--report") reportPath = *v;
      if (arg == "--journal") journalPath = *v;
      if (arg == "--write-exclusions") exclusionsOut = *v;
      if (arg == "--textures") textures = *v;
      if (arg == "--frames") frames = std::max(2, std::atoi(v->c_str()));
    } else if (!arg.empty() && arg[0] == '-') {
      std::cerr << "unknown option " << arg << "\n";
      printUsage();
      return 1;
    } else {
      roots.emplace_back(arg);
    }
  }
  if (roots.empty()) {
    printUsage();
    return 1;
  }
  if (!verify && (journalPath || exclusionsOut)) {
    std::cerr << "--journal and --write-exclusions need --verify\n";
    return 1;
  }

  for (const auto& file : excludeFiles) {
    const auto text = readFile(file);
    if (!text) {
      std::cerr << "can't read " << utf8(file) << "\n";
      return 1;
    }
    for (auto& path : core::parseExclusions(*text)) {
      options.exclude.insert(std::move(path));
    }
  }

  std::unique_ptr<mdw_convert::Verifier> verifier;
  std::unique_ptr<Journal> journal;
  std::size_t exact = 0;
  if (verify) {
    mdw_convert::Verifier::Settings settings;
    settings.frames = frames;
    if (textures) {
      settings.textureSearchPaths = {*textures};
    } else if (const auto content = engine::BundledContent::find()) {
      settings.textureSearchPaths = content->textureSearchPaths();
    }
    std::string error;
    verifier = mdw_convert::Verifier::create(settings, error);
    if (!verifier) {
      std::cerr << "--verify can't render here: " << error << "\n";
      return 1;
    }
    if (journalPath) {
      journal = std::make_unique<Journal>(*journalPath);
    }
    options.verify = [&](const std::filesystem::path& path, const core::MilkdawpPreset& preset) {
      const auto key = utf8(std::filesystem::absolute(path));
      if (journal) {
        if (const auto known = journal->find(key)) {
          return *known;
        }
        journal->start(key);
      }
      const auto result = verifier->check(preset.milk, preset);
      exact += result.exact ? 1 : 0;
      if (journal) {
        journal->done(key, result.reason,
                      result.exact ? std::string("exact")
                                   : "spread " + std::to_string(result.original) + " converted " +
                                         std::to_string(result.converted));
      }
      return result.reason;
    };
  }
  if (!quiet) {
    options.progress = [](std::size_t done, std::size_t total) {
      if (done % 50 == 0 || done == total) {
        std::cerr << "\r  " << done << " / " << total << std::flush;
        if (done == total) {
          std::cerr << "\n";
        }
      }
    };
  }

  std::ofstream report;
  if (reportPath) {
    report.open(*reportPath, std::ios::binary | std::ios::trunc);
    if (!report) {
      std::cerr << "can't write " << utf8(*reportPath) << "\n";
      return 1;
    }
    report << "path\toutcome\tdetail\tmacros\n";
  }

  std::map<core::ConvertOutcome, std::size_t> counts;
  std::vector<std::pair<std::string, std::string>> rejected;
  std::size_t macros = 0;
  std::size_t converted = 0;
  for (const auto& root : roots) {
    if (!std::filesystem::exists(root)) {
      std::cerr << utf8(root) << " doesn't exist\n";
      return 1;
    }
    if (!quiet) {
      std::cerr << "mdw-convert " << core::versionString() << ": " << utf8(root) << "\n";
    }
    for (const auto& entry : core::convertFolder(root, options)) {
      ++counts[entry.outcome];
      if (entry.outcome == core::ConvertOutcome::Written || entry.outcome == core::ConvertOutcome::Unchanged) {
        macros += entry.controls.size();
        ++converted;
      }
      if (entry.outcome == core::ConvertOutcome::Rejected) {
        rejected.emplace_back(entry.relativePath, entry.detail);
      }
      if (report) {
        std::string names;
        for (const auto& name : entry.controls) {
          names += (names.empty() ? "" : ", ") + name;
        }
        report << entry.relativePath << '\t' << core::outcomeName(entry.outcome) << '\t' << entry.detail << '\t'
               << names << '\n';
      }
    }
  }

  if (exclusionsOut) {
    // Only what projectM can't load is left out of the pack. A conversion that
    // "renders differently" is listed for a look, but kept: in the 2026-10-07
    // run that was always projectM's own render-to-render noise (ADR-0014).
    std::ofstream out(*exclusionsOut, std::ios::binary | std::ios::trunc);
    out << "# Presets left out of the shipped pack (milkdawp_preset_pack, tools/mdw-convert/CMakeLists.txt):\n"
           "# projectM can't load them at all. Written by mdw-convert --verify --write-exclusions.\n";
    for (const auto& [path, reason] : rejected) {
      const bool unloadable = reason.rfind("projectM can't load it:", 0) == 0; // the original, not ours
      out << "\n# " << reason << "\n" << (unloadable ? "" : "# kept: ") << path << "\n";
    }
  }

  const char* verb = options.dryRun ? "would write" : "written";
  std::cout << counts[core::ConvertOutcome::Written] << " " << verb << ", "
            << counts[core::ConvertOutcome::Unchanged] << " unchanged, " << counts[core::ConvertOutcome::Kept]
            << " kept (already there), " << counts[core::ConvertOutcome::Excluded] << " excluded, "
            << counts[core::ConvertOutcome::Rejected] << " rejected, " << counts[core::ConvertOutcome::Failed]
            << " failed";
  if (converted > 0) {
    std::cout << "; " << static_cast<double>(macros) / static_cast<double>(converted) << " Macros per preset";
  }
  if (verify) {
    std::cout << "; " << exact << " rendered exactly the same";
  }
  std::cout << "\n";
  return 0;
}
