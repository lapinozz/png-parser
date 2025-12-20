#include "inflater.hpp"

#include <fstream>
#include <string>
#include <string_view>
#include <vector>
#include <span>
#include <array>
#include <bit>
#include <spanstream>
#include <optional>
#include <print>

using Uint8Stream = std::basic_istream<std::uint8_t, std::char_traits<std::uint8_t>>;

template<std::integral T>
T readInt(std::istream& stream)
{
	T value;
	stream.read((char*)&value, sizeof(value));

	if constexpr (std::endian::native == std::endian::little)
	{
		value = std::byteswap(value);
	}

	return value;
}

template<std::size_t N>
std::array<std::uint8_t, N> readStaticBytes(std::istream& stream)
{
	std::array<std::uint8_t, N> bytes{};
	stream.read((char*)bytes.data(), bytes.size());

	return bytes;
}

std::vector<std::uint8_t> readDynamicBytes(std::istream& stream, int32_t count)
{
	std::vector<std::uint8_t> bytes{};
	bytes.resize(count);
	stream.read((char*)bytes.data(), bytes.size());

	return bytes;
}

struct PngChunkType
{
	std::array<std::uint8_t, 4> bytes;

	std::string_view toStr() const
	{
		return { (const char*)bytes.data(), 4 };
	}

	operator std::string_view() const
	{
		return toStr();
	}

	bool operator==(std::string_view other) const
	{
		return other == toStr();
	}
};

struct PngChunk
{
	std::int32_t length;
	PngChunkType type;
	std::vector<uint8_t> data;
	std::int32_t crc;
};

PngChunk readChunk(std::istream& stream)
{
	PngChunk chunk;

	chunk.length = readInt<std::int32_t>(stream);
	chunk.type.bytes = readStaticBytes<4>(stream);
	chunk.data = readDynamicBytes(stream, chunk.length);
	chunk.crc = readInt<std::int32_t>(stream);

	return chunk;
}

struct PngInfo
{
	uint32_t width;
	uint32_t height;
	uint8_t depth;
	uint8_t colorType;
	uint8_t compression;
	uint8_t filter;
	uint8_t interlace;
};

std::optional<PngInfo> readHeaderChunk(const PngChunk& chunk)
{
	if (chunk.type != "IHDR")
	{
		std::cerr << "Wrong first chunk type" << std::endl;
		return std::nullopt;
	}

	if (chunk.length != 13)
	{
		std::cerr << "Wrong first chunk length" << std::endl;
		return std::nullopt;
	}

	std::spanstream chunkStream(std::span<char>{(char*)chunk.data.data(), chunk.data.size()});

	PngInfo info;
	info.width			= readInt<uint32_t>(chunkStream);
	info.height			= readInt<uint32_t>(chunkStream);
	info.depth			= readInt<uint8_t>(chunkStream);
	info.colorType		= readInt<uint8_t>(chunkStream);
	info.compression	= readInt<uint8_t>(chunkStream);
	info.filter			= readInt<uint8_t>(chunkStream);
	info.interlace		= readInt<uint8_t>(chunkStream);

	if (info.width <= 0 || info.height <= 0)
	{
		std::cerr << "Invalid image size" << std::endl;
		return std::nullopt;
	}

	if (info.depth != 1 && info.depth != 2 && info.depth != 4 && info.depth != 8 && info.depth != 16)
	{
		std::cerr << "Invalid bit depth" << std::endl;
		return std::nullopt;
	}

	if (info.colorType != 0 && info.colorType != 2 && info.colorType != 3 && info.colorType != 4 && info.colorType != 6)
	{
		std::cerr << "Invalid color type" << std::endl;
		return std::nullopt;
	}

	if (info.compression != 0)
	{
		std::cerr << "Invalid compression method" << std::endl;
		return std::nullopt;
	}

	if (info.filter != 0)
	{
		std::cerr << "Invalid filter method" << std::endl;
		return std::nullopt;
	}

	if (info.interlace != 0 && info.interlace != 1)
	{
		std::cerr << "Invalid interlace method" << std::endl;
		return std::nullopt;
	}

	return info;
}

bool readPng(std::istream& stream)
{
	constexpr static std::array<std::uint8_t, 8> pngSignature{0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};

	const auto fileSignature = readStaticBytes<8>(stream);

	if (fileSignature != pngSignature)
	{
		std::cerr << "Incorrect file header" << std::endl;
		return false;
	}

	std::vector<PngChunk> chunks;

	while (stream)
	{
		chunks.emplace_back(readChunk(stream));

		auto& chunk = chunks.back();
				
		std::cout << "length: " << chunk.length << " type: " << chunk.type.toStr() << std::endl;

		if (chunk.type == "IEND")
		{
			break;
		}
	}

	if (chunks.size() <= 0)
	{
		std::cerr << "Empty file" << std::endl;
		return false;
	}

	auto& firstChunk = chunks.front();
	const auto pngInfo = readHeaderChunk(firstChunk);
	if (!pngInfo)
	{
		return false;
	}

	std::println("size: {}x{}", pngInfo->width, pngInfo->height);

	std::vector<std::uint8_t> compressedData;
	for (const auto& chunk : chunks)
	{
		if (chunk.type == "IDAT")
		{
			compressedData.insert(compressedData.end(), chunk.data.begin(), chunk.data.end());
		}
	}

	auto decompressedData = inflate(compressedData);

	return true;
}

int main()
{
	std::string testFolder = "C:/Users/gsq_apolomat/Downloads/PngSuite-2017jul19/";

	std::ifstream fileStream(testFolder + "basi2c08.png", std::ios_base::binary);

	readPng(fileStream);

	return 0;
}
