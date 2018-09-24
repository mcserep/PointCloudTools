#include <algorithm>
#include <numeric>
#include <unordered_set>

#include "ClusterMap.h"

namespace CloudTools
{
namespace DEM
{
GUInt32 ClusterMap::clusterIndex(int x, int y) const
{
	return _clusterPoints.at(OGRPoint(x, y));
}

std::vector<GUInt32> ClusterMap::clusterIndexes() const
{
	std::vector<GUInt32> indexes;
	indexes.reserve(_clusterIndexes.size());

	for (const auto& item : _clusterIndexes)
	{
		indexes.push_back(item.first);
	}
	return indexes;
}

void ClusterMap::addPoint(GUInt32 clusterIndex, int x, int y)
{
	OGRPoint point(x, y);

	if (_clusterIndexes.find(clusterIndex) == _clusterIndexes.end())
		throw std::out_of_range("Cluster is out of range.");

	if (std::find_if(_clusterIndexes[clusterIndex].begin(),
		_clusterIndexes[clusterIndex].end(),
		[&point](OGRPoint& p) { return point.Equals(&p); })
		!= _clusterIndexes[clusterIndex].end())
		throw std::logic_error("Point is already in cluster.");

	_clusterIndexes[clusterIndex].push_back(point);
	_clusterPoints.insert(std::make_pair(point, clusterIndex));
}

std::vector<OGRPoint> ClusterMap::neighbors(GUInt32 clusterIndex)
{
  std::unordered_set<OGRPoint, PointHash, PointEqual> neighbors;
  for (const OGRPoint& p : points(clusterIndex))
  {
    for (int i = p.getX() - 1; i <= p.getX() + 1; i++)
      for (int j = p.getY() - 1; j <= p.getY() + 1; j++)
      for (int j = p.getY() - 1; j <= p.getY() + 1; j++)
        if (i != p.getX() || j != p.getY())
        {
          OGRPoint point(i, j);
          auto it = _clusterPoints.find(point);
          if (it == _clusterPoints.end())
          {
            neighbors.insert(point);
          }
        }
  }

  return std::vector<OGRPoint>(neighbors.begin(), neighbors.end());
}

OGRPoint ClusterMap::getCenter(GUInt32 clusterIndex)
{
	auto csirip = points(clusterIndex);
	int avgX = std::accumulate(csirip.begin(), csirip.end(), 0, [](int value, OGRPoint& p) { return value + p.getX(); }) / csirip.size();
	int avgY = std::accumulate(csirip.begin(), csirip.end(), 0, [](int value, OGRPoint& p) { return value + p.getY(); }) / csirip.size();
	OGRPoint center(avgX, avgY);
	return center;
}

const std::vector<OGRPoint>& ClusterMap::points(GUInt32 clusterIndex) const
{
	return _clusterIndexes.at(clusterIndex);
}

void ClusterMap::createCluster(int x, int y)
{
	OGRPoint point(x, y);
	if (_clusterPoints.find(point) != _clusterPoints.end())
		throw std::logic_error("Point already in cluster map.");

	_clusterIndexes[_nextClusterIndex].push_back(point);
	_clusterPoints[point] = _nextClusterIndex;
	++_nextClusterIndex;
}

void ClusterMap::mergeClusters(GUInt32 clusterA, GUInt32 clusterB)
{
	if (_clusterIndexes.find(clusterA) == _clusterIndexes.end())
		throw std::out_of_range("The parameter cluster A is out of range.");
	if (_clusterIndexes.find(clusterB) == _clusterIndexes.end())
		throw std::out_of_range("The parameter cluster B is out of range.");
	if (clusterA == clusterB)
		return;

	// Merge the smaller cluster into the larger
	GUInt32 fromCluster = clusterB;
	GUInt32 toCluster = clusterA;
	if(_clusterIndexes[clusterB].size() > _clusterIndexes[clusterA].size())
	{
		fromCluster = clusterA;
		toCluster = clusterB;
	}

	// Update point to cluster map
	for (const auto& point : _clusterIndexes[fromCluster])
		_clusterPoints[point] = toCluster;

	// Update cluster to points map
	_clusterIndexes[toCluster].insert(
		_clusterIndexes[toCluster].end(),
		std::make_move_iterator(_clusterIndexes[fromCluster].begin()),
		std::make_move_iterator(_clusterIndexes[fromCluster].end()));
	
	// Remove merged cluster
	_clusterIndexes.erase(fromCluster);
}

void ClusterMap::removeCluster(GUInt32 clusterIndex)
{
	if (_clusterIndexes.find(clusterIndex) == _clusterIndexes.end())
		throw std::out_of_range("The specified cluster does not exist.");

	for (const auto& point : _clusterIndexes[clusterIndex])
		_clusterPoints.erase(point);
	_clusterIndexes.erase(clusterIndex);
}

std::size_t ClusterMap::removeSmallClusters(int threshold)
{
	std::vector<GUInt32> removedIndexes;
	for (const auto& item : _clusterIndexes)
		if(item.second.size() < threshold)
		{
			removedIndexes.push_back(item.first);
		}
	
	for(int index : removedIndexes)
	{
		removeCluster(index);
	}
	return removedIndexes.size();
}
} // DEM
} // CloudTools
