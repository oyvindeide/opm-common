/*
   Copyright 2016 Statoil ASA.

   This file is part of the Open Porous Media project (OPM).

   OPM is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   OPM is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with OPM.  If not, see <http://www.gnu.org/licenses/>.
   */

#include "EclIntegrationTest.hpp"
#include "EclRegressionTest.hpp"
#include "summaryIntegrationTest.hpp"
#include "summaryRegressionTest.hpp"

#include <opm/common/ErrorMacros.hpp>

#include <ert/util/util.h>
#include <ert/util/stringlist.h>
#include <ert/ecl/ecl_endian_flip.h>
#include <ert/ecl/ecl_file.h>

#include <iostream>
#include <string>
#include <getopt.h>

static void printHelp() {
    std::cout << "\ncompareECL compares ECLIPSE files (restart (.RST), unified restart (.UNRST), initial (.INIT), summary (.SMRY), unified summary (.UNSMRY) or .RFT) and gridsizes (from .EGRID or .GRID file) from two simulations.\n"
        << "The program takes four arguments:\n\n"
        << "1. Case number 1 (full path without extension)\n"
        << "2. Case number 2 (full path without extension)\n"
        << "3. Absolute tolerance\n"
        << "4. Relative tolerance (between 0 and 1)\n\n"
        << "In addition, the program takes these options (which must be given before the arguments):\n\n"
        << "-a Run a full analysis of errors.\n"
        << "-g Will print the vector with the greatest error ratio.\n"
        << "-h Print help and exit.\n"
        << "-i Execute integration test (regression test is default).\n"
        << "   The integration test compares SGAS, SWAT and PRESSURE in unified restart files, so this option can not be used in combination with -t.\n"
        << "-I Same as -i, but throws an exception when the number of keywords in the two cases differ. Can not be used in combination with -t.\n"
        << "-k Specify specific keyword to compare (capitalized), for example -k PRESSURE.\n"
        << "-K Will not allow different amount of keywords in the two files. Throws an exception if the amount are different.\n"
        << "-l Only do comparison for the last occurrence. This option is only for the regression test, and can therefore not be used in combination with -i or -I.\n"
        << "-m mainVar. Will calculate the error ratio for one main variable. Valid input is WOPR, WWPR, WGPR or WBHP.\n"
        << "-n Do not throw on errors.\n"
        << "-p Print keywords in both cases and exit. Can not be used in combination with -P.\n"
        << "-P Print common and uncommon keywords in both cases and exit. Can not be used in combination with -p.\n"
        << "-R Will allow comparison between a restarted simulation and a normal simulation for summary regression tests. The files must end at the same time.\n"
        << "-s int Sets the number of spikes that are allowed for each keyword in summary integration tests.\n"
        << "-t Specify ECLIPSE filetype to compare (unified restart is default). Can not be used in combination with -i or -I. Different possible arguments are:\n"
        << "    -t UNRST \t Compare two unified restart files (.UNRST). This the default value, so it is the same as not passing option -t.\n"
        << "    -t INIT  \t Compare two initial files (.INIT).\n"
        << "    -t RFT   \t Compare two RFT files (.RFT).\n"
        << "    -t RST   \t Compare two cases consisting of restart files (.Xnnnn).\n"
        << "    -t SMRY  \t Compare two cases consistent of (unified) summary files.\n"
        << "    -t RST1  \t Compare two cases where the first case consists of restart files (.Xnnnn), and the second case consists of a unified restart file (.UNRST).\n"
        << "    -t RST2  \t Compare two cases where the first case consists of a unified restart file (.UNRST), and the second case consists of restart files (.Xnnnn).\n"
        << "   Note that when dealing with restart files (.Xnnnn), the program concatenates all of them into one unified restart file, which is used for comparison and stored in the same directory as the restart files.\n"
        << "   This will overwrite any existing unified restart file in that directory.\n\n"
        << "-v For the rate keywords WOPR, WGPR, WWPR and WBHP. Calculates the error volume of the two summary files. This is printed to screen.\n"
        << "\nExample usage of the program: \n\n"
        << "compareECL -k PRESSURE <path to first casefile> <path to second casefile> 1e-3 1e-5\n"
        << "compareECL -t INIT -k PORO <path to first casefile> <path to second casefile> 1e-3 1e-5\n"
        << "compareECL -i <path to first casefile> <path to second casefile> 0.01 1e-6\n\n"
        << "Exceptions are thrown (and hence program exits) when deviations are larger than the specified "
        << "tolerances, or when the number of cells does not match -- either in the grid file or for a "
        << "specific keyword. Information about the keyword, keyword occurrence (zero based) and cell "
        << "coordinate is printed when an exception is thrown. For more information about how the cases "
        << "are compared, see the documentation of the EclFilesComparator class.\n\n";
}



void splitBasename(const std::string& basename, std::string& path, std::string& filename) {
    const size_t lastSlashIndex = basename.find_last_of("/\\");
    path = basename.substr(0,lastSlashIndex);
    filename = basename.substr(lastSlashIndex+1);
}



// Inspired by the ecl_pack application in the ERT library
void concatenateRestart(const std::string& basename) {
    std::string inputPath, inputBase;
    splitBasename(basename, inputPath, inputBase);
    stringlist_type* inputFiles = stringlist_alloc_new();
    const int numFiles = ecl_util_select_filelist(inputPath.c_str(), inputBase.c_str(), ECL_RESTART_FILE, false, inputFiles);

    const char* target_file_name = ecl_util_alloc_filename(inputPath.c_str(), inputBase.c_str(), ECL_UNIFIED_RESTART_FILE, false, -1);
    fortio_type* target = fortio_open_writer(target_file_name, false, ECL_ENDIAN_FLIP);
    int dummy;
    ecl_kw_type* seqnum_kw = ecl_kw_alloc_new("SEQNUM", 1, ECL_INT, &dummy);

    int reportStep = 0;
    for (int i = 0; i < numFiles; ++i) {
        ecl_util_get_file_type(stringlist_iget(inputFiles, i), nullptr, &reportStep);
        ecl_file_type* src_file = ecl_file_open(stringlist_iget(inputFiles, i), 0);
        ecl_kw_iset_int(seqnum_kw, 0, reportStep);
        ecl_kw_fwrite(seqnum_kw, target);
        ecl_file_fwrite_fortio(src_file, target, 0);
        ecl_file_close(src_file);
    }
    fortio_fclose(target);
    stringlist_free(inputFiles);
}

//------------------------------------------------//

int main(int argc, char** argv) {
    // Restart is default
    ecl_file_enum file_type      = ECL_UNIFIED_RESTART_FILE;
    // RegressionTest is default
    bool integrationTest         = false;
    bool allowDifferentAmount    = true;
    bool checkNumKeywords        = false;
    bool findGreatestErrorRatio  = false;
    bool findVolumeError         = false;
    bool onlyLastOccurrence      = false;
    bool printKeywords           = false;
    bool printKeywordsDifference = false;
    bool restartFile             = false;
    bool specificKeyword         = false;
    bool specificFileType        = false;
    bool allowSpikes             = false;
    bool throwOnError            = true;
    bool throwTooGreatErrorRatio = true;
    bool acceptExtraKeywords     = false;
    bool analysis                = false;
    bool volumecheck             = true;
    char* keyword                = nullptr;
    char* fileTypeCstr           = nullptr;
    const char* mainVariable     = nullptr;
    int c                        = 0;
    int spikeLimit               = -1;

    while ((c = getopt(argc, argv, "hiIk:alnpPt:VRgs:m:vKx")) != -1) {
        switch (c) {
            case 'a':
              analysis = true;
              break;
            case 'g':
                findGreatestErrorRatio = true;
                throwTooGreatErrorRatio = false;
                break;
            case 'h':
                printHelp();
                return 0;
            case 'i':
                integrationTest = true;
                break;
            case 'I':
                integrationTest = true;
                checkNumKeywords = true;
                break;
            case 'k':
                specificKeyword = true;
                keyword = optarg;
                break;
            case 'K':
                allowDifferentAmount = false;
                break;
            case 'l':
                onlyLastOccurrence = true;
                break;
            case 'm':
                mainVariable = optarg;
                break;
            case 'n':
                throwOnError = false;
                break;
            case 'p':
                printKeywords = true;
                break;
            case 'P':
                printKeywordsDifference = true;
                break;
            case 'R':
                restartFile = true;
                break;
            case 's':
                allowSpikes = true;
                spikeLimit = atof(optarg);
                break;
            case 't':
                specificFileType = true;
                fileTypeCstr = optarg;
                break;
            case 'v':
                findVolumeError = true;
                break;
            case 'V':
                volumecheck = false;
                break;
            case 'x':
                acceptExtraKeywords = true;
                break;
            case '?':
                if (optopt == 'k' || optopt == 'm' || optopt == 's') {
                    std::cerr << "Option " << optopt << " requires a keyword as argument, see manual (-h) for more information." << std::endl;
                    return EXIT_FAILURE;
                }
                else if (optopt == 't') {
                    std::cerr << "Option t requires an ECLIPSE filetype as argument, see manual (-h) for more information." << std::endl;
                    return EXIT_FAILURE;
                }
                else {
                    std::cerr << "Unknown option." << std::endl;
                    return EXIT_FAILURE;
                }
            default:
                return EXIT_FAILURE;
        }
    }
    int argOffset = optind;
    if ((printKeywords && printKeywordsDifference) ||
        (integrationTest && specificFileType)      ||
        (integrationTest && onlyLastOccurrence)) {
        std::cerr << "Error: Options given which can not be combined. "
            << "Please see the manual (-h) for more information." << std::endl;
        return EXIT_FAILURE;
    }

    if (argc != argOffset + 4) {
        std::cerr << "Error: The number of options and arguments given is not correct. "
            << "Please run compareECL -h to see manual." << std::endl;
        return EXIT_FAILURE;
    }

    std::string basename1 = argv[argOffset];
    std::string basename2 = argv[argOffset + 1];
    double absTolerance   = strtod(argv[argOffset + 2], nullptr);
    double relTolerance   = strtod(argv[argOffset + 3], nullptr);

    if (specificFileType) {
        std::string fileTypeString(fileTypeCstr);
        for (auto& ch: fileTypeString) ch = toupper(ch);
        if (fileTypeString== "UNRST") {} //Do nothing
        else if (fileTypeString == "RST") {
            concatenateRestart(basename1);
            concatenateRestart(basename2);
        }
        else if (fileTypeString == "RST1") {
            concatenateRestart(basename1);
        }
        else if (fileTypeString == "RST2") {
            concatenateRestart(basename2);
        }
        else if (fileTypeString == "INIT") {
            file_type = ECL_INIT_FILE;
        }
        else if (fileTypeString == "RFT") {
            file_type = ECL_RFT_FILE;
        }
        else if (fileTypeString == "SMRY")
            file_type = ECL_SUMMARY_FILE;
        else {
            std::cerr << "Unknown ECLIPSE filetype specified with option -t. Please run compareECL -h to see manual." << std::endl;
            return EXIT_FAILURE;
        }
    }

    if (restartFile && (file_type != ECL_SUMMARY_FILE || integrationTest)) {
        std::cerr << "Error: -R can only be used in for summary regression tests." << std::endl;
        return EXIT_FAILURE;
    }

    std::cout << "Comparing '" << basename1 << "' to '" << basename2 << "'." << std::endl;
    try {
        if (file_type == ECL_SUMMARY_FILE) {
            if(!integrationTest){
                SummaryRegressionTest compare(basename1,basename2,absTolerance,relTolerance);
                compare.throwOnErrors(throwOnError);
                compare.doAnalysis(analysis);
                compare.setPrintKeywords(printKeywords);
                compare.setIsRestartFile(restartFile);
                compare.setAllowDifferentNumberOfKeywords(acceptExtraKeywords);
                if(specificKeyword){
                    compare.getRegressionTest(keyword);
                }
                else{
                    compare.setPrintKeywords(printKeywords);
                    compare.getRegressionTest();
                }
            } else {
                SummaryIntegrationTest compare(basename1,basename2,absTolerance,relTolerance);
                compare.throwOnErrors(throwOnError);
                compare.setFindVectorWithGreatestErrorRatio(findGreatestErrorRatio);
                compare.setAllowSpikes(allowSpikes);
                if (mainVariable) {
                    compare.setOneOfTheMainVariables(true);
                    std::string str(mainVariable);
                    std::transform(str.begin(), str.end(),str.begin(), ::toupper);
                    if(str == "WOPR" ||str=="WWPR" ||str=="WGPR" || str == "WBHP"){
                        compare.setMainVariable(str);
                    }else{
                        throw std::invalid_argument("The input is not a main variable. -m option requires a valid main variable.");
                    }
                }
                compare.setFindVolumeError(findVolumeError);
                if (spikeLimit != -1) {
                    compare.setSpikeLimit(spikeLimit);
                }
                compare.setAllowDifferentAmountOfKeywords(allowDifferentAmount);
                compare.setPrintKeywords(printKeywords);
                compare.setThrowExceptionForTooGreatErrorRatio(throwTooGreatErrorRatio);
                if(specificKeyword){
                    compare.setPrintSpecificKeyword(specificKeyword);
                    compare.getIntegrationTest(keyword);
                    return 0;
                }
                compare.getIntegrationTest();
            }
        }
        else if (integrationTest) {
            ECLIntegrationTest comparator(basename1, basename2, absTolerance, relTolerance);
            if (printKeywords) {
                comparator.printKeywords();
                return 0;
            }
            if (printKeywordsDifference) {
                comparator.printKeywordsDifference();
                return 0;
            }
            if (checkNumKeywords) {
                comparator.equalNumKeywords();
            }
            if (specificKeyword) {
                if (comparator.elementInWhitelist(keyword)) {
                    comparator.resultsForKeyword(keyword);
                }
                else {
                    std::cerr << "Keyword " << keyword << " is not supported for the integration test. Use SGAS, SWAT or PRESSURE." << std::endl;
                    return EXIT_FAILURE;
                }
            }
            else {
                comparator.results();
            }
        }
        else {
            ECLRegressionTest comparator(file_type, basename1, basename2, absTolerance, relTolerance);
            comparator.throwOnErrors(throwOnError);
            comparator.doAnalysis(analysis);
            comparator.setAcceptExtraKeywords(acceptExtraKeywords);
            if (printKeywords) {
                comparator.printKeywords();
                return 0;
            }
            if (printKeywordsDifference) {
                comparator.printKeywordsDifference();
                return 0;
            }
            if (onlyLastOccurrence) {
                comparator.setOnlyLastOccurrence(true);
            }
            if (specificKeyword) {
                comparator.gridCompare(volumecheck);
                comparator.resultsForKeyword(keyword);
            }
            else {
                comparator.gridCompare(volumecheck);
                comparator.results();
            }
            if (comparator.getNoErrors() > 0)
              OPM_THROW(std::runtime_error, comparator.getNoErrors() << " errors encountered in comparisons.");
        }
    }
    catch (const std::exception& e) {
        std::cerr << "Program threw an exception: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }
    return 0;
}
