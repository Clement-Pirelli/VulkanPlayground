#pragma once
#include <vector>
#include "AttributeType.h"
#include <optional>

class OFile
{
public:

	OFile(){}

	struct FileData
	{
		std::vector<AttributeType> attributes = {};
		size_t vertexAmount = {};
		std::vector<std::byte> vertices = {};
		std::vector<uint32_t> indices = {};
	};

	static std::optional<OFile> load(const char* path);
	static bool save(const char* path, const FileData &data);

	[[nodiscard]]
	const std::vector<AttributeType> &attributes() const { return fileData.attributes; }

	[[nodiscard]]
	size_t vertexAmount() const { return fileData.vertexAmount; }
	[[nodiscard]]
	size_t vertexSize() const;

	[[nodiscard]]
	const std::vector<std::byte> &vertices() const { return fileData.vertices; };
	[[nodiscard]]
	const std::vector<uint32_t> &indices() const { return fileData.indices; };
	

private:

	FileData fileData = {};

};