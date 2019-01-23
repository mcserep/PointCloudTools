#include <iostream>
#include <string>
#include <utility>

#include <boost/program_options.hpp>
#include <gdal_priv.h>

#include <CloudTools.Common/IO/IO.h>
#include <CloudTools.Common/IO/Reporter.h>
#include <CloudTools.DEM/Comparers/Difference.hpp>
#include <CloudTools.DEM/SweepLineCalculation.hpp>
#include <CloudTools.DEM/ClusterMap.h>
#include <CloudTools.DEM/Filters/MorphologyFilter.hpp>
#include "NoiseFilter.h"
#include "MatrixTransformation.h"
#include "TreeCrownSegmentation.h"
#include "MorphologyClusterFilter.h"
#include "HausdorffDistance.h"

namespace po = boost::program_options;

using namespace CloudTools::IO;
using namespace CloudTools::DEM;
using namespace AHN::Vegetation;

GDALDataset* generateCanopyHeightModel(const std::string&, const std::string&, const std::string&,
                               CloudTools::IO::Reporter *reporter, po::variables_map& vm);

int countLocalMaximums(GDALDataset* target, CloudTools::IO::Reporter *reporter, po::variables_map& vm);

GDALDataset* antialias(GDALDataset* target, const std::string& outpath, CloudTools::IO::Reporter *reporter, po::variables_map& vm);

GDALDataset* eliminateNonTrees(GDALDataset* target, double threshold, CloudTools::IO::Reporter *reporter, po::variables_map& vm);

std::vector<OGRPoint> collectSeedPoints(GDALDataset* target, CloudTools::IO::Reporter *reporter, po::variables_map& vm);

ClusterMap treeCrownSegmentation(std::vector<OGRPoint>& seedPoints, CloudTools::IO::Reporter *reporter, po::variables_map& vm);

void morphologyFiltering(const MorphologyClusterFilter::Method method, ClusterMap& clusterMap, const std::string& outpath);

void calculateHausdorffDistance(ClusterMap& ahn2ClusterMap, ClusterMap& ahn3ClusterMap);

int main(int argc, char* argv[])
{
	std::string DTMinputPath;
	std::string DSMinputPath;
	std::string AHN2DSMinputPath;
	std::string AHN2DTMinputPath;
	std::string outputPath = (fs::current_path() / "out.tif").string();
	std::string AHN2outputPath = (fs::current_path() / "ahn2_out.tif").string();

	// Read console arguments
	po::options_description desc("Allowed options");
	desc.add_options()
		("ahn3-dtm-input-path,t", po::value<std::string>(&DTMinputPath), "DTM input path")
		("ahn3-dsm-input-path,s", po::value<std::string>(&DSMinputPath), "DSM input path")
		("ahn2-dtm-input-path,y", po::value<std::string>(&AHN2DTMinputPath), "AHN2 DTM input path")
		("ahn2-dsm-input-path,x", po::value<std::string>(&AHN2DSMinputPath), "AHN2 DSM input path")
		("output-path,o", po::value<std::string>(&outputPath)->default_value(outputPath), "output path")
		("verbose,v", "verbose output")
		("quiet,q", "suppress progress output")
		("help,h", "produce help message");

	po::variables_map vm;
	po::store(po::parse_command_line(argc, argv, desc), vm);
	po::notify(vm);

	// Argument validation
	if (vm.count("help"))
	{
		std::cout << "Compares an AHN-2 and AHN-3 tile pair and filters out changes in vegtation." << std::endl;
		std::cout << desc << std::endl;
		return Success;
	}

	bool argumentError = false;
	if (!vm.count("ahn3-dsm-input-path"))
	{
		std::cerr << "Surface input file is mandatory." << std::endl;
		argumentError = true;
	}

	if (!vm.count("ahn3-dtm-input-path"))
	{
		std::cerr << "Terrain input file is mandatory." << std::endl;
		argumentError = true;
	}

	if (!fs::exists(DSMinputPath))
	{
		std::cerr << "The surface input file does not exist." << std::endl;
		argumentError = true;
	}
	if (!fs::exists(DTMinputPath))
	{
		std::cerr << "The terrain input file does not exist." << std::endl;
		argumentError = true;
	}

	if (argumentError)
	{
		std::cerr << "Use the --help option for description." << std::endl;
		return InvalidInput;
	}

	// Program
	Reporter *reporter = vm.count("verbose")
		? static_cast<Reporter*>(new TextReporter())
		: static_cast<Reporter*>(new BarReporter());

	if (!vm.count("quiet"))
		std::cout << "=== AHN Vegetation Filter ===" << std::endl;

	GDALAllRegister();

	// Generate CHM
	Difference<float> *comparison = new Difference<float>({ DTMinputPath, DSMinputPath }, "CHM.tif");
	if (!vm.count("quiet"))
	{
		comparison->progress = [&reporter](float complete, const std::string &message)
		{
			reporter->report(complete, message);
			return true;
		};
	}
	comparison->execute();
	std::cout << "CHM generated." << std::endl;

	// Count local maximum values in CHM
	SweepLineCalculation<float> *countLocalMax = new SweepLineCalculation<float>(
		{ comparison->target() }, 1, nullptr);

	int counter = 0;
	countLocalMax->computation = [&countLocalMax, &counter](int x, int y, const std::vector<Window<float>> &sources)
	{
		const Window<float> &source = sources[0];
		if (!source.hasData())
			return;

		for (int i = -countLocalMax->range(); i <= countLocalMax->range(); i++)
			for (int j = -countLocalMax->range(); j <= countLocalMax->range(); j++)
				if (source.data(i, j) > source.data(0, 0))
					return;
		++counter;
	};

	if (!vm.count("quiet"))
	{
		countLocalMax->progress = [&reporter](float complete, const std::string &message)
		{
			reporter->report(complete, message);
			return true;
		};
	}
	reporter->reset();
	countLocalMax->execute();
	std::cout << "Number of local maximums are: " << counter << std::endl;


	// Vegetation filter
	/*NoiseFilter* filter = new NoiseFilter(comparison->target(), outputPath, 3);
	if (!vm.count("quiet"))
	{
		filter->progress = [&reporter](float complete, const std::string &message)
		{
			reporter->report(complete, message);
			return true;
		};
	}
	reporter->reset();
	filter->execute();*/

	// Perform anti-aliasing via convolution matrix
	MatrixTransformation *filter = new MatrixTransformation(comparison->target(), "antialias.tif", 1);
	if (!vm.count("quiet"))
	{
		filter->progress = [&reporter](float complete, const std::string &message)
		{
			reporter->report(complete, message);
			return true;
		};
	}
	reporter->reset();
	filter->execute();
	std::cout << "Matrix transformation performed." << std::endl;

	// Eliminate all points that are shorter than a possible tree
	float thresholdOfTrees = 1.5;
	SweepLineTransformation<float> *eliminateNonTrees = new SweepLineTransformation<float>(
		{ filter->target() }, "nosmall.tif", 0, nullptr);

	eliminateNonTrees->computation = [&eliminateNonTrees, &thresholdOfTrees](int x, int y,
		const std::vector<Window<float>> &sources)
	{
		const Window<float> &source = sources[0];
		if (!source.hasData() || source.data() < thresholdOfTrees)
			return static_cast<float>(eliminateNonTrees->nodataValue);
		else
			return source.data();
	};

	if (!vm.count("quiet"))
	{
		eliminateNonTrees->progress = [&reporter](float complete, const std::string &message)
		{
			reporter->report(complete, message);
			return true;
		};
	}
	reporter->reset();
	eliminateNonTrees->execute();
	std::cout << "Too small values eliminated." << std::endl;

	// Count & collect local maximum values
	SweepLineCalculation<float> *collectSeeds = new SweepLineCalculation<float>(
		{ eliminateNonTrees->target() }, 4, nullptr);

	counter = 0;
	std::vector<OGRPoint> seedPoints;
	collectSeeds->computation = [&collectSeeds, &counter, &seedPoints](int x, int y,
		const std::vector<Window<float>> &sources)
	{
		const Window<float> &source = sources[0];
		if (!source.hasData())
			return;

		for (int i = -collectSeeds->range(); i <= collectSeeds->range(); i++)
			for (int j = -collectSeeds->range(); j <= collectSeeds->range(); j++)
				if (source.data(i, j) > source.data(0, 0))
					return;
		++counter;
		seedPoints.emplace_back(x, y, source.data());
	};

	if (!vm.count("quiet"))
	{
		collectSeeds->progress = [&reporter](float complete, const std::string &message)
		{
			reporter->report(complete, message);
			return true;
		};
	}
	reporter->reset();
	collectSeeds->execute();
	std::cout << "Number of local maximums are: " << counter << std::endl;

	ClusterMap clusters;
	// Tree crown segmentation
	TreeCrownSegmentation *crownSegmentation = new TreeCrownSegmentation(
		{ eliminateNonTrees->target() }, seedPoints, nullptr);
	if (!vm.count("quiet"))
	{
		crownSegmentation->progress = [&reporter](float complete, const std::string &message)
		{
			reporter->report(complete, message);
			return true;
		};
	}
	reporter->reset();
	crownSegmentation->execute();
	std::cout << "Tree crown segmentation performed." << std::endl;
	clusters = crownSegmentation->clusterMap();

  MorphologyClusterFilter *morphologyFilterErosion = new MorphologyClusterFilter(
      clusters, "erosion.tif", MorphologyClusterFilter::Method::Erosion, nullptr);
  morphologyFilterErosion->threshold = 6;
  /*if (!vm.count("quiet"))
  {
    morphologyFilterErosion->progress = [&reporter](float complete, const std::string &message)
    {
      reporter->report(complete, message);
      return true;
    };
  }*/
  reporter->reset();
  morphologyFilterErosion->execute();
  std::cout << "Morphological erosion performed." << std::endl;

  MorphologyClusterFilter *morphologyFilterDilation = new MorphologyClusterFilter(
      morphologyFilterErosion->clusterMap, outputPath, MorphologyClusterFilter::Method::Dilation, nullptr);
  /*if (!vm.count("quiet"))
  {
    morphologyFilterDilation->progress = [&reporter](float complete, const std::string &message)
    {
      reporter->report(complete, message);
      return true;
    };
  }*/
  reporter->reset();
  morphologyFilterDilation->execute();
  std::cout << "Morphological dilation performed." << std::endl;

	if (vm.count("ahn2-dtm-input-path") && vm.count("ahn2-dsm-input-path"))
	{
		// CHM
		Difference<float> *ahn2comparison = new Difference<float>({ AHN2DTMinputPath, AHN2DSMinputPath }, "ahn2_CHM.tif");
		if (!vm.count("quiet"))
		{
			ahn2comparison->progress = [&reporter](float complete, const std::string &message)
			{
				reporter->report(complete, message);
				return true;
			};
		}
		ahn2comparison->execute();
		std::cout << "AHN2 CHM generated." << std::endl;

		// Anti-aliasing
		MatrixTransformation *ahn2filter = new MatrixTransformation(ahn2comparison->target(), "ahn2_antialias.tif", 1);
		if (!vm.count("quiet"))
		{
			ahn2filter->progress = [&reporter](float complete, const std::string &message)
			{
				reporter->report(complete, message);
				return true;
			};
		}
		reporter->reset();
		ahn2filter->execute();
		std::cout << "AHN2 Matrix transformation performed." << std::endl;

		// Eliminate small values
		SweepLineTransformation<float> *ahn2eliminateNonTrees = new SweepLineTransformation<float>(
			{ ahn2filter->target() }, "ahn2_nosmall.tif", 0, nullptr);

		ahn2eliminateNonTrees->computation = [&ahn2eliminateNonTrees, &thresholdOfTrees](int x, int y,
			const std::vector<Window<float>> &sources)
		{
			const Window<float> &source = sources[0];
			if (!source.hasData() || source.data() < thresholdOfTrees)
				return static_cast<float>(ahn2eliminateNonTrees->nodataValue);
			else
				return source.data();
		};

		if (!vm.count("quiet"))
		{
			ahn2eliminateNonTrees->progress = [&reporter](float complete, const std::string &message)
			{
				reporter->report(complete, message);
				return true;
			};
		}
		reporter->reset();
		ahn2eliminateNonTrees->execute();
		std::cout << "AHN2 Too small values eliminated." << std::endl;

		// Collect seeds
		SweepLineCalculation<float> *ahn2collectSeeds = new SweepLineCalculation<float>(
			{ ahn2eliminateNonTrees->target() }, 4, nullptr);

		counter = 0;
		std::vector<OGRPoint> ahn2seedPoints;
		ahn2collectSeeds->computation = [&ahn2collectSeeds, &counter, &ahn2seedPoints](int x, int y,
			const std::vector<Window<float>> &sources)
		{
			const Window<float> &source = sources[0];
			if (!source.hasData())
				return;

			for (int i = -ahn2collectSeeds->range(); i <= ahn2collectSeeds->range(); i++)
				for (int j = -ahn2collectSeeds->range(); j <= ahn2collectSeeds->range(); j++)
					if (source.data(i, j) > source.data(0, 0))
						return;
			++counter;
			ahn2seedPoints.emplace_back(x, y);
		};

		if (!vm.count("quiet"))
		{
			ahn2collectSeeds->progress = [&reporter](float complete, const std::string &message)
			{
				reporter->report(complete, message);
				return true;
			};
		}
		reporter->reset();
		ahn2collectSeeds->execute();
		std::cout << "AHN2 Number of local maximums are: " << counter << std::endl;

		ClusterMap clusterMap;
		// Tree crown segmentation
		TreeCrownSegmentation *ahn2crownSegmentation = new TreeCrownSegmentation(
			{ ahn2eliminateNonTrees->target() }, ahn2seedPoints, nullptr);
		if (!vm.count("quiet"))
		{
			ahn2crownSegmentation->progress = [&reporter](float complete, const std::string &message)
			{
				reporter->report(complete, message);
				return true;
			};
		}
		reporter->reset();
		ahn2crownSegmentation->execute();
		std::cout << "AHN2 Tree crown segmentation performed." << std::endl;
		clusterMap = ahn2crownSegmentation->clusterMap();

		// Morphological opening
		MorphologyClusterFilter *ahn2morphologyFilterErosion = new MorphologyClusterFilter(
            ahn2crownSegmentation->clusterMap(), "ahn2_erosion.tif", MorphologyClusterFilter::Method::Erosion, nullptr);
		ahn2morphologyFilterErosion->threshold = 6;
		ahn2morphologyFilterErosion->execute();
		std::cout << "AHN2 Morphological erosion performed." << std::endl;
		clusterMap = ahn2morphologyFilterErosion->clusterMap;

		MorphologyClusterFilter *ahn2morphologyFilterDilation = new MorphologyClusterFilter(
			ahn2morphologyFilterErosion->clusterMap, "temp.tif", MorphologyClusterFilter::Method::Dilation, nullptr);
		ahn2morphologyFilterDilation->execute();
		std::cout << "AHN2 Morphological dilation performed." << std::endl;
        clusterMap = ahn2morphologyFilterDilation->clusterMap;

		Difference<> *CHMDifference = new Difference<>(std::vector<GDALDataset*>({ comparison->target(), ahn2comparison->target() }), "CHM_diff.tif");
		if (!vm.count("quiet"))
		{
			CHMDifference->progress = [&reporter](float complete, const std::string &message)
			{
				reporter->report(complete, message);
				return true;
			};
		}
		CHMDifference->execute();
		std::cout << "CHM difference generated." << std::endl;

		HausdorffDistance *distance = new HausdorffDistance(ahn2morphologyFilterDilation->clusterMap, morphologyFilterDilation->clusterMap);
		distance->execute();
		/*for (auto elem : distance->lonelyAHN2())
		{
			//std::cout << elem.first.first << "    " << elem.first.second << "    " << elem.second << std::endl;
			std::cout << elem << std::endl;
		}
		std::cout << "AHN3 LONELY INDEXES" << std::endl;
		for (auto elem : distance->lonelyAHN3())
		{
			//std::cout << elem.first.first << "    " << elem.first.second << "    " << elem.second << std::endl;
			std::cout << elem << std::endl;
		}*/
		std::cout << "Hausdorff-distance calculated." << std::endl;

		GDALDriver* driver = GetGDALDriverManager()->GetDriverByName("GTiff");
		if (driver == nullptr)
			throw std::invalid_argument("Target output format unrecognized.");

		if (fs::exists(AHN2outputPath) &&
				driver->Delete(AHN2outputPath.c_str()) == CE_Failure &&
				!fs::remove(AHN2outputPath))
			throw std::runtime_error("Cannot overwrite previously created target file.");

		GDALDataset* target = driver->Create(AHN2outputPath.c_str(),
										ahn2crownSegmentation->targetMetadata().rasterSizeX(), ahn2crownSegmentation->targetMetadata().rasterSizeY(), 1,
										gdalType<int>(), nullptr);
		if (target == nullptr)
			throw std::runtime_error("Target file creation failed.");

        target->SetGeoTransform(&ahn2crownSegmentation->targetMetadata().geoTransform()[0]);

		GDALRasterBand* targetBand = target->GetRasterBand(1);
		targetBand->SetNoDataValue(-1);

		int commonId;
		for (auto elem : distance->closest())
		{
			commonId = 1;
			auto center = clusterMap.center(elem.first.first);
			CPLErr ioResult = targetBand->RasterIO(GF_Write,
												   center.getX(), center.getY(),
												   1, 1,
												   &commonId,
												   1, 1,
												   gdalType<int>(),
												   0, 0);

			center = clusters.center(elem.first.second);
			ioResult = targetBand->RasterIO(GF_Write,
												   center.getX(), center.getY(),
												   1, 1,
												   &commonId,
												   1, 1,
												   gdalType<int>(),
												   0, 0);

			if (ioResult != CE_None)
				throw std::runtime_error("Target write error occured.");
		}

		for (auto elem : distance->lonelyAHN2())
		{
			commonId = 2;
			auto center = clusterMap.center(elem);
			CPLErr ioResult = targetBand->RasterIO(GF_Write,
												   center.getX(), center.getY(),
												   1, 1,
												   &commonId,
												   1, 1,
												   gdalType<int>(),
												   0, 0);
		}

		for (auto elem : distance->lonelyAHN3())
		{
			commonId = 3;
			auto center = clusters.center(elem);
			CPLErr ioResult = targetBand->RasterIO(GF_Write,
												   center.getX(), center.getY(),
												   1, 1,
												   &commonId,
												   1, 1,
												   gdalType<int>(),
												   0, 0);
		}

		GDALClose(target);

		// Calculate differences between dimensions
    std::map<std::pair<GUInt32, GUInt32>, std::pair<double, double>> diff;
    for (const auto& elem : distance->closest())
    {
      auto centerAHN2 = clusterMap.center(elem.first.first);
      auto centerAHN3 = clusters.center(elem.first.second);
      double vertical = centerAHN3.getZ() - centerAHN2.getZ();
      diff.insert(std::make_pair(elem.first, std::make_pair(elem.second, vertical)));
      std::cout << elem.first.first << "; " << elem.first.second << " horizontal distance: " << elem.second << ", vertical distance: " << vertical << std::endl;
      /*if(vertical == 23) {
        std::cout << "centerAHN2 X: " << centerAHN2.getX() << std::endl;
        std::cout << "centerAHN2 Y: " << centerAHN2.getY() << std::endl;
        std::cout << "centerAHN3 X: " << centerAHN3.getX() << std::endl;
        std::cout << "centerAHN3 Y: " << centerAHN3.getY() << std::endl;
      }*/
    }

		delete distance;
		delete CHMDifference;
		delete ahn2morphologyFilterDilation;
		delete ahn2morphologyFilterErosion;
		delete ahn2crownSegmentation;
		delete ahn2collectSeeds;
		delete ahn2eliminateNonTrees;
		delete ahn2filter;
		delete ahn2comparison;
	}

  delete morphologyFilterDilation;
	delete morphologyFilterErosion;
	delete crownSegmentation;
	delete countLocalMax;
	delete comparison;
	delete filter;
	delete collectSeeds;
	delete reporter;
	return Success;
}

bool reporter(CloudTools::DEM::SweepLineTransformation<float> *transformation, CloudTools::IO::Reporter *reporter)
{
	transformation->progress = [&reporter](float complete, const std::string &message)
	{
		reporter->report(complete, message);
		return true;
	};
	return false;
}

GDALDataset* generateCanopyHeightModel(const std::string& DTMinput, const std::string& DSMinput, const std::string& outpath,
                               CloudTools::IO::Reporter *reporter, po::variables_map& vm)
{
  Difference<float> *comparison = new Difference<float>({ DTMinput, DSMinput }, outpath);
  if (!vm.count("quiet"))
  {
    comparison->progress = [&reporter](float complete, const std::string &message)
    {
      reporter->report(complete, message);
      return true;
    };
  }
  comparison->execute();
  reporter->reset();
  return comparison->target();
}

int countLocalMaximums(GDALDataset* target, CloudTools::IO::Reporter *reporter, po::variables_map& vm)
{
  SweepLineCalculation<float> *countLocalMax = new SweepLineCalculation<float>(
    { target }, 1, nullptr);

  int counter = 0;
  countLocalMax->computation = [&countLocalMax, &counter](int x, int y, const std::vector<Window<float>> &sources)
  {
    const Window<float> &source = sources[0];
    if (!source.hasData())
      return;

    for (int i = -countLocalMax->range(); i <= countLocalMax->range(); i++)
      for (int j = -countLocalMax->range(); j <= countLocalMax->range(); j++)
        if (source.data(i, j) > source.data(0, 0))
          return;
    ++counter;
  };

  if (!vm.count("quiet"))
  {
    countLocalMax->progress = [&reporter](float complete, const std::string &message)
    {
      reporter->report(complete, message);
      return true;
    };
  }
  reporter->reset();
  countLocalMax->execute();

  return counter;
}

GDALDataset* antialias(GDALDataset* target, const std::string& outpath, CloudTools::IO::Reporter *reporter, po::variables_map& vm)
{
  MatrixTransformation *filter = new MatrixTransformation(target, outpath, 1);
  if (!vm.count("quiet"))
  {
    filter->progress = [&reporter](float complete, const std::string &message)
    {
      reporter->report(complete, message);
      return true;
    };
  }
  reporter->reset();
  filter->execute();
  return filter->target();
}

GDALDataset* eliminateNonTrees(GDALDataset* target, double threshold, CloudTools::IO::Reporter *reporter, po::variables_map& vm)
{
  SweepLineTransformation<float> *eliminateNonTrees = new SweepLineTransformation<float>(
    { target }, "ahn2_nosmall.tif", 0, nullptr);

  eliminateNonTrees->computation = [&eliminateNonTrees, &threshold](int x, int y, const std::vector<Window<float>> &sources)
  {
    const Window<float> &source = sources[0];
    if (!source.hasData() || source.data() < threshold)
      return static_cast<float>(eliminateNonTrees->nodataValue);
    else
      return source.data();
  };

  if (!vm.count("quiet"))
  {
    eliminateNonTrees->progress = [&reporter](float complete, const std::string &message)
    {
      reporter->report(complete, message);
      return true;
    };
  }
  reporter->reset();
  eliminateNonTrees->execute();
  return eliminateNonTrees->target();
}

std::vector<OGRPoint> collectSeedPoints(GDALDataset* target, CloudTools::IO::Reporter *reporter, po::variables_map& vm)
{
  SweepLineCalculation<float> *collectSeeds = new SweepLineCalculation<float>(
    { target }, 4, nullptr);

  std::vector<OGRPoint> seedPoints;
  collectSeeds->computation = [&collectSeeds, &seedPoints](int x, int y, const std::vector<Window<float>> &sources)
  {
    const Window<float> &source = sources[0];
    if (!source.hasData())
      return;

    for (int i = -collectSeeds->range(); i <= collectSeeds->range(); i++)
      for (int j = -collectSeeds->range(); j <= collectSeeds->range(); j++)
        if (source.data(i, j) > source.data(0, 0))
          return;
    seedPoints.emplace_back(x, y);
  };

  if (!vm.count("quiet"))
  {
    collectSeeds->progress = [&reporter](float complete, const std::string &message)
    {
      reporter->report(complete, message);
      return true;
    };
  }
  reporter->reset();
  collectSeeds->execute();
  return seedPoints;
}

ClusterMap treeCrownSegmentation(GDALDataset* target, std::vector<OGRPoint>& seedPoints,
                           CloudTools::IO::Reporter *reporter, po::variables_map& vm)
{
  TreeCrownSegmentation *crownSegmentation = new TreeCrownSegmentation(
    { target }, seedPoints, nullptr);
  if (!vm.count("quiet"))
  {
    crownSegmentation->progress = [&reporter](float complete, const std::string &message)
    {
      reporter->report(complete, message);
      return true;
    };
  }
  reporter->reset();
  crownSegmentation->execute();
  return crownSegmentation->clusterMap();
}

void morphologyFiltering(const MorphologyClusterFilter::Method method, ClusterMap& clusterMap, const std::string& outpath)
{
  MorphologyClusterFilter *morphologyFilterErosion = new MorphologyClusterFilter(
    clusterMap, outpath, method, nullptr);
  morphologyFilterErosion->threshold = 6;
  morphologyFilterErosion->execute();
}

void calculateHausdorffDistance(ClusterMap& ahn2ClusterMap, ClusterMap& ahn3ClusterMap)
{
  HausdorffDistance *distance = new HausdorffDistance(ahn2ClusterMap, ahn3ClusterMap);
  distance->execute();
}
