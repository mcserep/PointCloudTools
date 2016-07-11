#include "Result.h"

#include <boost/algorithm/string/predicate.hpp>

namespace AHN
{
namespace Buildings
{
#pragma region Result

Result::~Result()
{
	if (dataset != nullptr)
		GDALClose(dataset);
}

Result::Result(Result&& other)
{
	_path = other._path;
	dataset = other.dataset;

	other._path.clear();
	other.dataset = nullptr;
}

Result& Result::operator=(Result&& other)
{
	if (this == &other)
		return *this;

	_path = other._path;
	dataset = other.dataset;

	other._path.clear();
	other.dataset = nullptr;
	return *this;
}

#pragma endregion

#pragma region TemporaryFileResult

TemporaryFileResult::~TemporaryFileResult()
{
	fs::remove(_path);
}

#pragma endregion

#pragma region VirtualResult

VirtualResult::VirtualResult(const std::string& path, GDALDataset* dataset)
	: Result(boost::starts_with(path, "/vsimem/") ? path : "/vsimem/" + path, dataset)
{ }

VirtualResult::VirtualResult(const fs::path& path, GDALDataset* dataset)
	: Result(boost::starts_with(path.string(), "/vsimem/") ? path : (fs::path("/vsimem/") / path), dataset)
{ }

VirtualResult::~VirtualResult()
{
	VSIUnlink(_path.string().c_str());
}

#pragma endregion
} // Buildings
} // AHN
