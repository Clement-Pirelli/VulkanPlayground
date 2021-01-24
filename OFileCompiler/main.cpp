#include "Files.h"
#include "Logger/Logger.h"
#include <unordered_map>
#include <algorithm>
#pragma warning(push, 0)
#define TINYOBJLOADER_IMPLEMENTATION
#include "tiny_obj_loader.h"
#pragma warning(pop)
#include <optional>
#pragma warning(disable : 26451)
#define RETURNCHECK(expr, errmsg) if(!(expr)) { Logger::logError(errmsg); return -1; }
#include "OFileSerialization.h"
#include <iostream>
#include "Serializer.h"
#include "vec.h"

using ObjAttribute = tinyobj::attrib_t;
using ObjShape = tinyobj::shape_t;


struct ObjVertex
{
	vec3 pos = {};
	vec2 uv = {};
	vec3 normal = {};
	vec3 color = {};

	bool operator==(const ObjVertex &other) const {
		return pos == other.pos && uv == other.uv && normal == other.normal && color == other.color;
	}
};

namespace std {
	template<> struct hash<ObjVertex> {
		size_t operator()(const ObjVertex &vertex) const {
			const auto posUvHash = (hash<vec3>()(vertex.pos) << 1)
				^ (hash<vec2>()(vertex.uv));

			const auto normalColorHash = (hash<vec3>()(vertex.normal) >> 1)
				^ (hash<vec3>()(vertex.color));

			return posUvHash ^ normalColorHash;
		}
	};
}

[[nodiscard]]
bool loadObj(const std::string &path, ObjAttribute &attrib, std::vector<ObjShape> &shapes)
{
	std::vector<tinyobj::material_t> materials;
	std::string warn, err;

	tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, path.c_str());
	if (!warn.empty()) Logger::logWarning(warn.c_str());
	if (!err.empty())
	{
		Logger::logError(err.c_str());
		return false;
	}
	return true;
}

[[nodiscard]]
OFile::FileData processObj(const ObjAttribute &attrib, const std::vector<ObjShape> &shapes)
{
	const bool hasUV = attrib.texcoords.size() > 0;
	const bool hasNormals = attrib.normals.size() > 0;
	const bool hasColors = attrib.colors.size() > 0;

	Logger::logMessageFormatted(
		"This model %s UVs",
		(hasUV) ? "has" : "doesn't have"
	);

	Logger::logMessageFormatted(
		"This model %s per-vertex colors",
		(hasColors) ? "has" : "doesn't have"
	);

	Logger::logMessageFormatted(
		"This model %s per-vertex normals",
		(hasNormals) ? "has" : "doesn't have"
	);

	std::vector<AttributeType> attributes
	{
		AttributeType::vec3
	};

	if (hasUV)		attributes.push_back(AttributeType::vec2);
	if (hasNormals)	attributes.push_back(AttributeType::vec3);
	if (hasColors)	attributes.push_back(AttributeType::vec3);

	std::unordered_map<ObjVertex, uint32_t> uniqueVertices = {};
	std::vector<ObjVertex> vertices = {};
	std::vector<uint32_t> indices = {};

	for (const auto &shape : shapes)
		for (const auto &index : shape.mesh.indices) {
			ObjVertex vertex
			{
				.pos
				{
					attrib.vertices[3 * index.vertex_index + 0],
					attrib.vertices[3 * index.vertex_index + 1],
					attrib.vertices[3 * index.vertex_index + 2]
				}
			};

			if (hasUV)
			{
				vertex.uv = {
						attrib.texcoords[2 * index.texcoord_index + 0],
						1.0f - attrib.texcoords[2 * index.texcoord_index + 1]
				};
			}

			if (hasNormals)
			{
				vertex.normal = {
						attrib.normals[3 * index.normal_index + 0],
						attrib.normals[3 * index.normal_index + 1],
						attrib.normals[3 * index.normal_index + 2]
				};
			}

			if (hasColors)
			{
				vertex.color = {
						attrib.colors[3 * index.vertex_index + 0],
						attrib.colors[3 * index.vertex_index + 1],
						attrib.colors[3 * index.vertex_index + 2]
				};
			}

			if (uniqueVertices.count(vertex) == 0) {
				uniqueVertices[vertex] = static_cast<uint32_t>(vertices.size());
				vertices.push_back(vertex);
			}

			indices.push_back(uniqueVertices[vertex]);
		}

	OFile::FileData result
	{
		.attributes = std::move(attributes),
		.vertexAmount = uniqueVertices.size(),
		.indices = std::move(indices),
	};

	size_t kbWritten = 0;

	if (hasUV && hasNormals && hasColors)
	{
		result.vertices.resize(vertices.size() * sizeof(ObjVertex));
		memcpy(result.vertices.data(), vertices.data(), result.vertices.size());

		kbWritten = result.vertices.size() / 1024;

	}
	else
	{
		std::vector<uint8_t> vertexData = std::vector<uint8_t>(vertices.size() * sizeof(ObjVertex));
		StreamOut stream(vertexData.data(), vertexData.size());
		for (auto &vertex : vertices)
		{
			stream.setNext(vertex.pos);
			if (hasUV) stream.setNext(vertex.uv);
			if (hasNormals) stream.setNext(vertex.normal);
			if (hasColors) stream.setNext(vertex.color);
		}

		result.vertices = std::move(vertexData);
		result.vertices.resize(stream.bytesWritten());

		kbWritten = result.vertices.size() / 1024;
	}

	Logger::logMessageFormatted(
		"%u individual vertices found, which take %u KB.",
		uniqueVertices.size(),
		kbWritten
	);

	return result;
}

int main(int argc, char *argv[])
{
	Logger::setVerbosity(Logger::Verbosity::TRIVIAL);

	std::string inputPath = {};
	std::string outputPath = {};

	for (int i = 0; i < argc; i++)
	{
		const std::string argument = std::string(argv[i]);
		if (argument.compare("-src") == 0)
		{
			i++;
			if (i >= argc) break;
			inputPath = std::string(argv[i]);
		}
		else if (argument.compare("-dst") == 0)
		{
			i++;
			if (i >= argc) break;
			outputPath = std::string(argv[i]);
		}
	}

	if (inputPath.empty() || outputPath.empty())
	{
		Logger::logError("Could not parse src and dst arguments - aborting");
		return -1;
	}

	RETURNCHECK(inputPath.ends_with(".obj"), "This asset compiler only parses .objs. Please provide an obj file!");
	Logger::logMessageFormatted("----- Processing model at path: %s to destination %s -----", inputPath.c_str(), outputPath.c_str());

	ObjAttribute attrib;
	std::vector<ObjShape> shapes;

	const bool succeeded = loadObj(inputPath, attrib, shapes);
	RETURNCHECK(succeeded, "Couldn't load .obj!");

	RETURNCHECK(attrib.vertices.size() > 0, ("Model at " + std::string(inputPath) + " has no vertex positions!").c_str());

	std::cout << std::endl;

	const auto processingResult = processObj(attrib, shapes);

	if (OFile::save(outputPath.c_str(), processingResult))
	{
		Logger::logMessage("File was outputed successfully!");
		return 0;
	}
	else
	{
		Logger::logError("File could not be written to!");
		return -1;
	}
}