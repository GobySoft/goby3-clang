#include <iostream>

#include "clang/Frontend/FrontendActions.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Tooling.h"

#include "llvm/Support/CommandLine.h"

#include "actions.h"

namespace cl = llvm::cl;

// Apply a custom category to all command-line options so that they are the
// only ones displayed.
static cl::OptionCategory Goby3ToolCategory("goby3-tool options");

// CommonOptionsParser declares HelpMessage with a description of the common
// command-line options related to the compilation database and input files.
// It's nice to have this help message in all tools.
static cl::extrahelp CommonHelp(clang::tooling::CommonOptionsParser::HelpMessage);

static cl::opt<bool>
    Generate("gen",
             cl::desc("Run generate action (create YML interface files from C++ source code)"),
             cl::cat(Goby3ToolCategory));
static cl::opt<bool> Visualize(
    "viz",
    cl::desc("Run visualize action (create GraphViz DOT files from multiple YML interface files)"),
    cl::cat(Goby3ToolCategory));

static cl::opt<std::string> Target("target",
                                   cl::desc("Specify target (binary) name for 'gen' action"),
                                   cl::value_desc("name"), cl::cat(Goby3ToolCategory));

static cl::opt<std::string> OutDir("outdir",
                                   cl::desc("Specify output directory for 'viz' and 'gen' actions"),
                                   cl::value_desc("dir"), cl::init("."),
                                   cl::cat(Goby3ToolCategory));

static cl::opt<std::string> Deployment(
    "deployment",
    cl::desc(
        "Specify deployment name for 'viz' action that summarizes the collection of yml files or the path to a deployment yml file"),
    cl::value_desc("name"), cl::cat(Goby3ToolCategory));

int main(int argc, const char** argv)
{
    clang::tooling::CommonOptionsParser OptionsParser(argc, argv, Goby3ToolCategory);

    if (Generate)
    {
        if (Target.empty())
        {
            std::cerr << "Must specify -target when using -gen" << std::endl;
            exit(EXIT_FAILURE);
        }
        clang::tooling::ClangTool Tool(OptionsParser.getCompilations(),
                                       OptionsParser.getSourcePathList());
        return goby::clang::generate(Tool, OutDir, Target);
    }
    else if (Visualize)
    {
        return goby::clang::visualize(OptionsParser.getSourcePathList(), OutDir, Deployment);
    }
    else
    {
        std::cerr << "Must specify an action (e.g. -gen or -viz)" << std::endl;
        exit(EXIT_FAILURE);
    }
}
