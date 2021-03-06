#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <utility>
#include <stdexcept>

#include <boost/program_options.hpp>
#include <boost/filesystem.hpp>
#include <gdal_priv.h>

#include <CloudTools.Common/IO/IO.h>
#include <CloudTools.Common/IO/Reporter.h>
#include <CloudTools.DEM/Comparers/Difference.hpp>

namespace po = boost::program_options;
namespace fs = boost::filesystem;

using namespace CloudTools::DEM;
using namespace CloudTools::IO;

int main(int argc, char* argv[]) try
{
	std::vector<std::string> inputPaths;
	std::string outputPath = (fs::current_path() / "out.tif").string();
	std::string outputFormat;
	std::vector<std::string> outputOptions;

	GDALDataType dataType;
	std::string dataTypeString;

	// Read console arguments
	po::options_description desc("Allowed options");
	desc.add_options()
		("input-path,i", po::value<std::vector<std::string>>(&inputPaths), "input path")
		("output-path,o", po::value<std::string>(&outputPath)->default_value(outputPath), "output path")
		("output-format,f", po::value<std::string>(&outputFormat)->default_value("GTiff"),
			"output format, supported formats:\n"
			"http://www.gdal.org/formats_list.html")
		("output-option", po::value<std::vector<std::string>>(&outputOptions),
			"output options, supported options:\n"
			"http://www.gdal.org/formats_list.html")
		("max-threshold", po::value<double>()->default_value(1000), "maximum difference threshold")
		("min-threshold", po::value<double>()->default_value(0), "minimum difference threshold")
		("datatype,d", po::value<std::string>(&dataTypeString)->default_value("Float32"),
			"data type of the input and output files, supported:\n"
		    "Int16, Int32, Float32, Float64")
		("nodata-value", po::value<double>(), "specifies the nodata value")
		("srs", po::value<std::string>(), "override spatial reference system")
		("verbose,v", "verbose output")
		("quiet,q", "suppress progress output")
		("help,h", "produce help message")		
		;

	po::variables_map vm;
	po::store(po::parse_command_line(argc, argv, desc), vm);
	po::notify(vm);

	// Post-processing arguments
	dataType = gdalType(dataTypeString);

	// Argument validation
	if (vm.count("help"))
	{
		std::cout << "Compares DEMs of same area to retrieve differences." << std::endl;
		std::cout << desc << std::endl;
		return Success;
	}

	bool argumentError = false;
	if (inputPaths.size() < 2)
	{
		std::cerr << "At least 2 input files must be given." << std::endl;
		argumentError = true;
	}

	if (dataType == GDALDataType::GDT_Unknown)
	{
		std::cerr << "Unrecognized data type." << std::endl;
		argumentError = true;
	}

	if (argumentError)
	{
		std::cerr << "Use the --help option for description." << std::endl;
		return InvalidInput;
	}

	// Program
	if (vm.count("verbose"))
		std::cout << "=== DEM Difference Tool ===" << std::endl;

	Reporter *reporter = vm.count("verbose")
		? static_cast<Reporter*>(new TextReporter())
		: static_cast<Reporter*>(new BarReporter());

	// Define comparer with corresponding data type
	GDALAllRegister();
	Transformation *comparison;	
	switch (dataType)
	{
	case GDALDataType::GDT_Int16:
		{
			auto difference = new Difference<GInt16>(inputPaths, outputPath);
			difference->maximumThreshold = vm["max-threshold"].as<double>();
			difference->minimumThreshold = vm["min-threshold"].as<double>();
			comparison = difference;
			break;
		}
	case GDALDataType::GDT_Int32:
		{
			auto difference = new Difference<GInt32>(inputPaths, outputPath);
			difference->maximumThreshold = vm["max-threshold"].as<double>();
			difference->minimumThreshold = vm["min-threshold"].as<double>();
			comparison = difference;
			break;
		}
	case GDALDataType::GDT_Float32:
		{
			auto difference = new Difference<float>(inputPaths, outputPath);
			difference->maximumThreshold = vm["max-threshold"].as<double>();
			difference->minimumThreshold = vm["min-threshold"].as<double>();
			comparison = difference;
			break;
		}
	case GDALDataType::GDT_Float64:
		{
			auto difference = new Difference<double>(inputPaths, outputPath);
			difference->maximumThreshold = vm["max-threshold"].as<double>();
			difference->minimumThreshold = vm["min-threshold"].as<double>();
			comparison = difference;
			break;
		}
	default:
		// Unsigned and complex types are not supported.
		std::cerr << "Unsupported data type given." << std::endl;
		return Unsupported;
	}

	comparison->targetFormat = outputFormat;
	if (vm.count("nodata-value"))
		comparison->nodataValue = vm["nodata-value"].as<double>();
	if (vm.count("srs"))
		comparison->spatialReference = vm["srs"].as<std::string>();
	if (!vm.count("quiet"))
	{
		comparison->progress = [&reporter](float complete, const std::string &message)
		{
			reporter->report(complete, message);
			return true;
		};
	}
	if (vm.count("output-option"))
	{
		for (const std::string &option : outputOptions)
		{
			auto pos = std::find(option.begin(), option.end(), '=');
			if (pos == option.end()) continue;
			std::string key = option.substr(0, pos - option.begin());
			std::string value = option.substr(pos - option.begin() + 1, option.end() - pos - 1);
			comparison->createOptions.insert(std::make_pair(key, value));
		}
	}

	// Display input metadata
	if (vm.count("verbose"))
	{
		std::cout << std::endl << "--- Input files ---" << std::endl;
		for (std::string &path : inputPaths)
		{
			const RasterMetadata &metadata = comparison->sourceMetadata(path);
			std::cout << "File path: \t" << path << std::endl;
			std::cout << metadata << std::endl;
		}

		if (!readBoolean("Would you like to continue?"))
		{
			std::cerr << "Operation aborted." << std::endl;
			return UserAbort;
		}
	}

	// Prepare operation
	comparison->prepare();
	
	// Display overall difference metadata
	if (vm.count("verbose"))
	{
		std::cout << std::endl << "--- Output file ---" << std::endl;
		const RasterMetadata& metadata = comparison->targetMetadata();
		std::cout << "File path: \t" << outputPath << std::endl
			<< metadata << std::endl;

		if (!readBoolean("Would you like to continue?"))
		{
			std::cerr << "Operation aborted." << std::endl;
			return UserAbort;
		}
	}

	// Execute operation
	comparison->execute();

	delete comparison;
	delete reporter;
	return Success;
}
catch (std::exception &ex)
{
	std::cerr << "ERROR: " << ex.what() << std::endl;
	return UnexcpectedError;
}
