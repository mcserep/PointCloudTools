#pragma once

#include <CloudTools.Common/Operation.h>
#include <CloudTools.DEM/ClusterMap.h>
#include <CloudTools.DEM/DatasetTransformation.hpp>

namespace AHN
{
namespace Vegetation
{
class HausdorffDistance : public CloudTools::Operation
{
public:
	double maximumDistance = 9.0;
	CloudTools::DEM::ClusterMap AHN2ClusterMap;
	CloudTools::DEM::ClusterMap AHN3ClusterMap;

	HausdorffDistance(CloudTools::DEM::ClusterMap& AHN2clusterMap,
	                  CloudTools::DEM::ClusterMap& AHN3clusterMap,
	                  Operation::ProgressType progress = nullptr)
		: AHN2ClusterMap(AHN2clusterMap), AHN3ClusterMap(AHN3clusterMap)
	{
	}

	double clusterDistance(GUInt32, GUInt32);
	GUInt32 closestCluster(GUInt32);
	std::map<std::pair<GUInt32, GUInt32>, double> distances() const;
	const std::map<std::pair<GUInt32, GUInt32>, double>& closest() const;
	const std::vector<GUInt32>& lonelyAHN2() const;
	const std::vector<GUInt32>& lonelyAHN3() const;

private:
	std::map<std::pair<GUInt32, GUInt32>, double> hausdorffDistances;
	std::map<std::pair<GUInt32, GUInt32>, double> hausdorffDistances2;
	std::map<std::pair<GUInt32, GUInt32>, double> closestClusters;
	std::vector<GUInt32> lonelyClustersAHN2;
	std::vector<GUInt32> lonelyClustersAHN3;

	void onPrepare() override
	{
	}

	void onExecute() override;
};
}
}
