#include "OFileSerialization.h"
#pragma warning(disable : 26451)
#include "Files.h"
#include "Logger/Logger.h"
#include "Serializer.h"

#define WRITER_CHECK(expr) if(!(expr)) return false;

std::optional<OFile> OFile::load(const char* path)
{
	OFile file;
	OFile::FileData &fileData = file.fileData;

	FileReader reader(path);
	if(reader.failed())
	{
		return std::nullopt;
	}
	std::vector<uint8_t> data = std::vector<uint8_t>(reader.calculateLength());
	reader.read((char *)data.data(), data.size());

	StreamIn stream(data.data(), data.size());
	fileData.attributes.resize(stream.getNext<size_t>());
	stream.getNext(fileData.attributes.data(), fileData.attributes.size());

	uint16_t objectNumber = stream.getNext<uint16_t>();
	(void *)objectNumber;

	fileData.vertexAmount = stream.getNext<size_t>();
	fileData.vertices.resize(file.vertexSize() * fileData.vertexAmount);
	stream.getNext(fileData.vertices.data(), fileData.vertices.size());

	fileData.indices.resize(stream.getNext<size_t>());
	stream.getNext(fileData.indices.data(), fileData.indices.size());

	return file;
}

bool OFile::save(const char* path, const OFile::FileData &data)
{
	FileWriter writer(path);

	WRITER_CHECK(writer.write(data.attributes.size()));
	WRITER_CHECK(writer.writeVector(data.attributes));

	uint16_t objectNumber = 1; //todo: support multiple objects in the same file
	WRITER_CHECK(writer.write(objectNumber));

	WRITER_CHECK(writer.write(data.vertexAmount));
	WRITER_CHECK(writer.writeVector(data.vertices));

	WRITER_CHECK(writer.write(data.indices.size()));
	WRITER_CHECK(writer.writeVector(data.indices));

	return true;
}

#undef WRITER_CHECK

size_t OFile::vertexSize() const
{
	size_t size = 0;
	for (AttributeType attribute : fileData.attributes)
	{
		size += attributeTypeToSize(attribute);
	}

	return size;
}
