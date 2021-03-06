#pragma once

#include <string>

#include "../Window.hpp"
#include "../SweepLineTransformation.hpp"

namespace CloudTools
{
namespace DEM
{
/// <summary>
/// Represents a noise filter for DEM datasets.
/// </summary>
template <typename DataType = float>
class MorphologyFilter : public SweepLineTransformation<DataType>
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

public:
	/// <summary>
	/// Initializes a new instance of the class. Loads input metadata and defines computation.
	/// </summary>
	/// <param name="sourceDataset">The source path of the filter.</param>
	/// <param name="targetPath">The target file of the filter.</param>
	/// <param name="mode">The applied morphology method.</param>
	/// <param name="progress">The callback method to report progress.</param>
	MorphologyFilter(const std::string& sourcePath,
	                 const std::string& targetPath,
	                 Method method = Method::Dilation,
					 Operation::ProgressType progress = nullptr)
		: SweepLineTransformation<DataType>({ sourcePath }, targetPath, 1, nullptr, progress),
		  method(method)
	{
		initialize();
	}

	/// <summary>
	/// Initializes a new instance of the class. Loads input metadata and defines computation.
	/// </summary>
	/// <param name="sourceDataset">The source dataset of the filter.</param>
	/// <param name="targetPath">The target file of the filter.</param>
	/// <param name="mode">The applied morphology method.</param>
	/// <param name="progress">The callback method to report progress.</param>
	MorphologyFilter(GDALDataset* sourceDataset,
		             const std::string& targetPath,
		             Method method = Method::Dilation,
					 Operation::ProgressType progress = nullptr)
		: SweepLineTransformation<DataType>({ sourceDataset }, targetPath, 1, nullptr, progress),
		  method(method)
	{
		initialize();
	}

	MorphologyFilter(const MorphologyFilter&) = delete;
	MorphologyFilter& operator=(const MorphologyFilter&) = delete;

private:
	/// <summary>
	/// Initializes the new instance of the class.
	/// </summary>
	void initialize();
};

template <typename DataType>
void MorphologyFilter<DataType>::initialize()
{
	// https://en.wikipedia.org/wiki/Mathematical_morphology
	// https://www.cs.auckland.ac.nz/courses/compsci773s1c/lectures/ImageProcessing-html/topic4.htm

	this->computation = [this](int x, int y, const std::vector<Window<DataType>>& sources)
	{
		if (this->method == Method::Dilation && this->threshold == -1)
			this->threshold = 0;
	  	if (this->method == Method::Erosion && this->threshold == -1)
	  		this->threshold = 9;

		const Window<DataType>& source = sources[0];

		float sum = 0;
		int counter = 0;
		for (int i = -1; i <= 1; ++i)
			for (int j = -1; j <= 1; ++j)
				if (source.hasData(i, j))
				{
					sum += source.data(i, j);
					++counter;
				}

		if (this->method == Method::Dilation && !source.hasData() && counter > this->threshold)
			return sum / counter;
		if (this->method == Method::Erosion && source.hasData() && counter < this->threshold)
			return static_cast<DataType>(this->nodataValue);
		return source.hasData() ? source.data() : static_cast<DataType>(this->nodataValue);
	};
	this->nodataValue = 0;
}
} // DEM
} // CloudTools
