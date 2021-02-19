#include "OFileSerialization.h"
#pragma warning(disable : 26451)
#include "Files.h"
#include "Logger/Logger.h"
#include "Serializer.h"
#include <lz4/lz4.h>

#define WRITER_CHECK(expr) if(!(expr)) return false;

size_t sizeForAttributes(const std::vector<AttributeType> &attributes)
{
	size_t size = 0;
	for (AttributeType attribute : attributes)
	{
		size += attributeTypeToSize(attribute);
	}

	return size;
}

OFile::FileData parseFileData(std::byte* bytes, size_t size)
{
	OFile::FileData fileData;
	StreamIn stream(bytes, size);
	fileData.attributes.resize(stream.getNext<size_t>());
	stream.getNext(fileData.attributes.data(), fileData.attributes.size());

	uint16_t objectNumber = stream.getNext<uint16_t>();
	(void *)objectNumber;

	fileData.vertexAmount = stream.getNext<size_t>();
	fileData.vertices.resize(sizeForAttributes(fileData.attributes) * fileData.vertexAmount);
	stream.getNext(fileData.vertices.data(), fileData.vertices.size());

	fileData.indices.resize(stream.getNext<size_t>());
	stream.getNext(fileData.indices.data(), fileData.indices.size());

	return fileData;
}

std::optional<OFile> OFile::load(const char* path)
{
	OFile file;
	OFile::FileData &fileData = file.fileData;

	FileReader reader(path);
	if(reader.failed())
	{
		return std::nullopt;
	}

	std::vector<std::byte> compressedData = reader.readInto<std::vector<std::byte>>();
	StreamIn stream(compressedData.data(), compressedData.size());
	const size_t uncompressedSize = stream.getNext<size_t>();
	
	std::vector<uint8_t> data = std::vector<uint8_t>(uncompressedSize);

	LZ4_decompress_safe((const char*)(compressedData.data()+sizeof(uncompressedSize)), (char*)data.data(), (int)compressedData.size()-sizeof(uncompressedSize), (int)data.size());
	file.fileData = parseFileData((std::byte*)data.data(), data.size());
	
	return file;
}

bool OFile::save(const char* path, const OFile::FileData &data)
{
	StretchyStreamOut streamOut = StretchyStreamOut();

	streamOut.setNext(data.attributes.size());
	streamOut.setNext(data.attributes.data(), data.attributes.size());

	constexpr uint16_t objectNumber = 1; //todo: support multiple objects in the same file
	streamOut.setNext(objectNumber);

	streamOut.setNext(data.vertexAmount);
	streamOut.setNext(data.vertices.data(), data.vertices.size());

	streamOut.setNext(data.indices.size());
	streamOut.setNext(data.indices.data(), data.indices.size());

	const int compressStaging = LZ4_compressBound((int)streamOut.bytesWritten());
	std::vector<std::byte> compressed(compressStaging);
	const int compressedSize = LZ4_compress_default((const char *)streamOut.getData(), (char*)compressed.data(), (int)streamOut.bytesWritten(), compressStaging);
	compressed.resize(compressedSize);

	FileWriter writer(path);
	WRITER_CHECK(writer.write(streamOut.bytesWritten()));
	WRITER_CHECK(writer.writeVector(compressed));

	return true;
}

#undef WRITER_CHECK

size_t OFile::vertexSize() const
{
	return sizeForAttributes(attributes());
}
