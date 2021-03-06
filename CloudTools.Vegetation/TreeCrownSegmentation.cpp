#include "TreeCrownSegmentation.h"

using namespace CloudTools;
using namespace CloudTools::DEM;

namespace CloudTools
{
namespace Vegetation
{
void TreeCrownSegmentation::initialize()
{
	this->computation = [this](int sizeX, int sizeY)
	{
		clusters.setSizeX(sizeX);
		clusters.setSizeY(sizeY);

		// Create initial clusters from seed points
		for (const auto& point : this->seedPoints)
		{
			clusters.createCluster(point.getX(), point.getY(), point.getZ());
		}

		bool hasChanged;
		double currentVerticalDistance = initialVerticalDistance;
		do
		{
			std::map<GUInt32, std::set<OGRPoint, PointComparator>> expandPoints;
			for (GUInt32 index : clusters.clusterIndexes())
				expandPoints.insert(std::make_pair(index, expandCluster(index, currentVerticalDistance)));

			hasChanged = false;
			std::vector<GUInt32> indexes = clusters.clusterIndexes();
			std::map<GUInt32, GUInt32> mergePairs;
			for (std::size_t i = 0; i < indexes.size(); ++i)
			{
				for (std::size_t j = i + 1; j < indexes.size(); ++j)
				{
					GUInt32 index_i = indexes[i];
					GUInt32 index_j = indexes[j];

					std::vector<OGRPoint> intersection;
					std::set_intersection(expandPoints[index_i].begin(), expandPoints[index_i].end(),
					                      expandPoints[index_j].begin(), expandPoints[index_j].end(),
					                      std::back_inserter(intersection), PointComparator());

					double oneSeedHeight = clusters.seedPoint(index_i).getZ();
					double otherSeedHeight = clusters.seedPoint(index_j).getZ();

					for (const OGRPoint& point : intersection)
					{
						double pointHeight = point.getZ();
						double diff = oneSeedHeight - pointHeight + otherSeedHeight - pointHeight;
						double normalizedDiff = diff / std::min(oneSeedHeight, otherSeedHeight);

						if (normalizedDiff < 1.0
						    && mergePairs.count(index_j) == 0 && mergePairs.count(index_i) == 0)
						{
							mergePairs[index_i] = index_j;
							mergePairs[index_j] = index_i;
							break;
						}
					}
				}
			}

			for (const auto& pair : mergePairs)
			{
				if (pair.first < pair.second)
					clusters.mergeClusters(pair.first, pair.second);
			}

			for (const auto& pair : expandPoints)
			{
				indexes = clusters.clusterIndexes();
				GUInt32 index = pair.first;

				if (std::find(indexes.begin(), indexes.end(), index) == indexes.end())
				{
					index = mergePairs[index];
				}

				for (const auto& point : pair.second)
				{
					try
					{
						clusters.clusterIndex(point.getX(), point.getY());
					}
					catch (std::out_of_range&)
					{
						clusters.addPoint(index, point.getX(), point.getY(), point.getZ());
						hasChanged = true;
					}
				}
			}

			if (currentVerticalDistance < maxVerticalDistance)
				currentVerticalDistance = std::min(currentVerticalDistance + increaseVerticalDistance, maxVerticalDistance);
		}
		while (hasChanged || currentVerticalDistance < maxVerticalDistance);
	};
}

std::set<OGRPoint, PointComparator> TreeCrownSegmentation::expandCluster(
	GUInt32 index, double verticalThreshold)
{
	std::set<OGRPoint, PointComparator> expand;
	OGRPoint center = clusters.center2D(index);

	for (const OGRPoint& p : clusters.neighbors(index))
	{
		double horizontalDistance = std::sqrt(std::pow(center.getX() - p.getX(), 2.0)
		                                      + std::pow(center.getY() - p.getY(), 2.0));
		double verticalDistance = std::abs(sourceData(p.getX(), p.getY())
		                                   - sourceData(clusters.seedPoint(index).getX(),
		                                                clusters.seedPoint(index).getY()));

		if (this->hasSourceData(p.getX(), p.getY()) && horizontalDistance <= maxHorizontalDistance
		    && verticalDistance <= verticalThreshold)
			expand.insert(OGRPoint(p.getX(), p.getY(), sourceData(p.getX(), p.getY())));
	}

	return expand;
}

ClusterMap& TreeCrownSegmentation::clusterMap()
{
	return this->clusters;
}
} // Vegetation
} // CloudTools
