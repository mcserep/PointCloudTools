#pragma once

#include <CloudTools.Common/Operation.h>
#include <CloudTools.DEM/ClusterMap.h>
#include <CloudTools.DEM/DatasetCalculation.hpp>

namespace AHN
{
namespace Vegetation
{
class MorphologyClusterFilter : public CloudTools::DEM::DatasetCalculation<float>
{
public:
	enum Method
	{
		Dilation,
		Erosion
	};

	/// <summary>
	/// The applied morphology method.
	/// </summary>
	Method method;

	/// <summary>
	/// Threshold value for morphology filter.
	///
	/// Default value is -1, which will be 0 for dilation and 9 for erosion.
	/// </summary>
	int threshold = -1;

private:
	CloudTools::DEM::ClusterMap _clusterMap;

public:
	/// <summary>
	/// Initializes a new instance of the class. Loads input metadata and defines computation.
	/// </summary>
	/// <param name="sourceDataset">The source path of the filter.</param>
	/// <param name="targetPath">The target file of the filter.</param>
	/// <param name="mode">The applied morphology method.</param>
	/// <param name="progress">The callback method to report progress.</param>
	MorphologyClusterFilter(CloudTools::DEM::ClusterMap& source,
	                        const std::vector<GDALDataset*>& sourceDatasets,
	                        ComputationType computation,
	                        Method method = Method::Dilation,
	                        ProgressType progress = nullptr)
		: DatasetCalculation(sourceDatasets, computation, progress),
		  _clusterMap(source), method(method)
	{
		initialize();
	}

	MorphologyClusterFilter(const MorphologyClusterFilter&) = delete;

	MorphologyClusterFilter& operator=(const MorphologyClusterFilter&) = delete;

	CloudTools::DEM::ClusterMap& target();

private:
	void initialize();
};
} // Vegetation
} // AHN