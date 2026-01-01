#include <SFML/Graphics.hpp>

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
#include <iostream>

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

std::optional<sf::Image> readPng(std::istream& stream)
{
	constexpr static std::array<std::uint8_t, 8> pngSignature{0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};

	const auto fileSignature = readStaticBytes<8>(stream);

	if (fileSignature != pngSignature)
	{
		std::cerr << "Incorrect file header" << std::endl;
		return std::nullopt;
	}

	std::vector<PngChunk> chunks;

	while (stream)
	{
		chunks.emplace_back(readChunk(stream));

		auto& chunk = chunks.back();

		if (chunk.type == "IEND")
		{
			break;
		}
	}

	if (chunks.size() <= 0)
	{
		std::cerr << "Empty file" << std::endl;
		return std::nullopt;
	}

	auto& firstChunk = chunks.front();
	const auto pngInfo = readHeaderChunk(firstChunk);
	if (!pngInfo)
	{
		return std::nullopt;
	}

	std::array<uint8_t, 256> paletteR{};
	std::array<uint8_t, 256> paletteG{};
	std::array<uint8_t, 256> paletteB{};
	std::array<uint8_t, 256> paletteA{};
	std::ranges::fill(paletteA, 255);

	std::optional<std::uint16_t> transR;
	std::optional<std::uint16_t> transG;
	std::optional<std::uint16_t> transB;

	std::vector<std::uint8_t> compressedData;
	for (const auto& chunk : chunks)
	{
		if (chunk.type == "IDAT")
		{
			compressedData.insert(compressedData.end(), chunk.data.begin(), chunk.data.end());
		}
		else if (chunk.type == "PLTE")
		{
			for (int x = 0; x < chunk.length / 3; x++)
			{
				paletteR[x] = chunk.data[x * 3 + 0];
				paletteG[x] = chunk.data[x * 3 + 1];
				paletteB[x] = chunk.data[x * 3 + 2];
			}
		}
		else if (chunk.type == "tRNS")
		{
			if (pngInfo->colorType == 0)
			{
				transR = std::byteswap(*(std::uint16_t*)chunk.data.data());
			}
			else if (pngInfo->colorType == 2)
			{
				transR = std::byteswap(*((std::uint16_t*)chunk.data.data() + 0));
				transG = std::byteswap(*((std::uint16_t*)chunk.data.data() + 1));
				transB = std::byteswap(*((std::uint16_t*)chunk.data.data() + 2));
			}
			else if (pngInfo->colorType == 3)
			{
				std::copy(chunk.data.begin(), chunk.data.end(), paletteA.begin());
			}
		}
	}

	auto decompressedDataOpt = inflate(compressedData);
	if (!decompressedDataOpt)
	{
		return std::nullopt;
	}

	auto& decompressedData = *decompressedDataOpt;

	int channels = 1;
	if (pngInfo->colorType == 2)
	{
		channels = 3;
	}
	else if (pngInfo->colorType == 4)
	{
		channels = 2;
	}
	else if (pngInfo->colorType == 6)
	{
		channels = 4;
	}

	const auto depth = pngInfo->depth;

	const auto bytePerChannel = depth == 16 ? 2 : 1;
	const auto bytePerPixel = channels * bytePerChannel;
	const auto lineByteWidth = pngInfo->width * bytePerPixel;

	const auto outputByteLength = pngInfo->width * pngInfo->height * 4;

	std::vector<uint8_t> imageData(std::max(outputByteLength, lineByteWidth * pngInfo->height));

	static constexpr uint8_t scaleTable[]{ 0, 0xff, 0x55, 0, 0x11, 0, 0, 0, 0x01 };
	const auto scale = pngInfo->colorType == 3 ? 1 : scaleTable[depth];

	int pos{};
	const auto nextByte = [&]()
	{
		return decompressedData[pos++];
	};
	
	const auto rawImageWidth = [&](int width) -> int
	{
		if (depth < 8)
		{
			// return std::ceil((channels * depth * width) / 8.f);

			std::uint16_t v = (channels * depth * width);
			bool b = v & 0b111;
			v >>= 3;
			v += b || (v == 0);

			return v;
		}
		
		return channels * width * bytePerChannel;
	};

	const auto unfilter = [&](auto& data, uint8_t filter, int x, int y, int lineWidth)
	{
		std::uint8_t byte = nextByte();

		std::uint8_t a = 0;
		std::uint8_t b = 0;
		std::uint8_t c = 0;

		if (x >= bytePerPixel)
		{
			a = data[x - bytePerPixel + y * lineWidth];
		}

		if (y >= 1)
		{
			b = data[x + (y - 1) * lineWidth];
		}

		if (x >= bytePerPixel && y >= 1)
		{
			c = data[x - bytePerPixel + (y - 1) * lineWidth];
		}

		if (filter == 1)
		{
			byte += a;
		}
		else if (filter == 2)
		{
			byte += b;
		}
		else if (filter == 3)
		{
			byte += ((int)a + b) / 2;
		}
		else if (filter == 4)
		{
			const auto p = (int)a + b - c;
			const auto pa = std::abs(p - a);
			const auto pb = std::abs(p - b);
			const auto pc = std::abs(p - c);

			int v{};
			if (pa <= pb && pa <= pc)
			{
				v = a;
			}
			else if (pb <= pc)
			{
				v = b;
			}
			else
			{
				v = c;
			}

			byte += v;
		}

		data[x + y * lineWidth] = byte;
	};

	const auto readRawImage = [&](auto& data, int width, int height, int startX, int startY, int strideX, int strideY)
	{
		const auto byteWidth = rawImageWidth(width);

		for (int y = 0; y < height; y++)
		{
			const auto filter = nextByte();

			for (int x = 0; x < byteWidth; x++)
			{
				unfilter(data, filter, x, y, byteWidth);
			}
		}

		const auto pPerB = std::min(width, 8 / depth);

		auto* passByte = data.data();

		int i{};

		auto row = startY;
		while (row < pngInfo->height)
		{
			auto col = startX;
			while (col < pngInfo->width)
			{
				for (int x = 0; x < bytePerPixel; x++)
				{
					if (depth >= 8)
					{
						imageData[col * bytePerPixel + row * lineByteWidth + x] = *(passByte++);
					}
					else
					{
						imageData[col * bytePerPixel + row * lineByteWidth + x] = scale * (*passByte >> (8 - depth));

						*passByte <<= depth;

						if (++i == pPerB)
						{
							i = 0;
							passByte++;
						}
					}
				}

				col += strideX;
			}

			if (i != 0)
			{
				passByte++;
				i = 0;
			}

			row += strideY;
		}
	};

	if (!pngInfo->interlace)
	{
		auto tempData = imageData;
		readRawImage(tempData, pngInfo->width, pngInfo->height, 0, 0, 1, 1);
	}
	else
	{
		static constexpr std::array<int, 7> startXTable = { 0, 0, 4, 0, 2, 0, 1 };
		static constexpr std::array<int, 7> startYTable = { 0, 4, 0, 2, 0, 1, 0 };
		static constexpr std::array<int, 7> strideYTable = { 8, 8, 8, 4, 4, 2, 2 };
		static constexpr std::array<int, 7> strideXTable = { 8, 8, 4, 4, 2, 2, 1 };

		for (int pass{}; pass < 7; pass++)
		{
			const auto strideX = strideXTable[pass];
			const auto strideY = strideYTable[pass];

			const auto startX = startYTable[pass];
			const auto startY = startXTable[pass];

			const auto passWidth = (pngInfo->width - startX + strideX - 1) / strideX;
			const auto passHeight = (pngInfo->height - startY + strideY - 1) / strideY;

			if (!passWidth || !passHeight)
			{
				continue;
			}

			const auto passByteWidth = rawImageWidth(passWidth);
			const auto passSize = passByteWidth * passHeight;

			std::vector<uint8_t> passData;
			passData.resize(passSize);

			readRawImage(passData, passWidth, passHeight, startX, startY, strideX, strideY);
		}
	}

	if (depth == 16)
	{
		for (int x = 0; x < pngInfo->width * pngInfo->height * channels; x++)
		{
			imageData[x] = ((uint16_t*)imageData.data())[x] & 0xFF;
		}
	}

	if (pngInfo->colorType == 3)
	{
		auto out = imageData.data() + pngInfo->width * pngInfo->height * 4;
		auto in = imageData.data() + pngInfo->width * pngInfo->height;

		for (int x = 0; x < pngInfo->width * pngInfo->height; x++)
		{
			out -= 4;
			in--;

			out[3] = paletteA[*in];
			out[2] = paletteB[*in];
			out[1] = paletteG[*in];
			out[0] = paletteR[*in];
		}
	}
	else if (channels < 4)
	{
		auto out = imageData.data() + pngInfo->width * pngInfo->height * 4;
		auto in = imageData.data() + pngInfo->width * pngInfo->height * channels;

		for (int x = 0; x < pngInfo->width * pngInfo->height; x++)
		{
			out -= 4;
			in -= channels;

			if (channels == 1)
			{
				out[3] = 0xff;
				out[2] = *in;
				out[1] = *in;
				out[0] = *in;
			}
			else if (channels == 2)
			{
				out[3] = in[1];
				out[2] = in[0];
				out[1] = in[0];
				out[0] = in[0];
			}
			else if (channels == 3)
			{
				out[3] = 0xff;
				out[2] = in[2];
				out[1] = in[1];
				out[0] = in[0];
			}
		}
	}

	if (transR)
	{
		if (depth < 8)
		{
			transR = *transR * scale;

			if (transG)
			{
				transG = *transG * scale;
				transB = *transB * scale;
			}
		}
		else if (depth == 16)
		{
			transR = *transR >> 8;

			if (transG)
			{
				transG = *transG >> 8;
				transB = *transB >> 8;
			}
		}

		if (pngInfo->colorType == 0)
		{
			auto in = imageData.data();
			for (int x = 0; x < pngInfo->width * pngInfo->height; x++)
			{
				if (in[0] == *transR)
				{
					in[3] = 0;
				}

				in += 4;
			}
			
		}
		else if (pngInfo->colorType == 2)
		{
			auto in = imageData.data();
			for (int x = 0; x < pngInfo->width * pngInfo->height; x++)
			{
				if (in[0] == *transR && in[1] == *transG && in[2] == *transB)
				{
					in[3] = 0;
				}

				in += 4;
			}
		}
		else if (pngInfo->colorType == 2 || pngInfo->colorType == 6)
		{
		}
	}


	sf::Image image({ pngInfo->width, pngInfo->height }, imageData.data());

	return image;
}

int main()
{
	std::string testFolder = TEST_FILES_DIR;

	sf::Texture texture;
	sf::Sprite sprite(texture);

	sf::Texture textureRef;
	sf::Sprite spriteRef(textureRef);

	sf::Texture textureDiff;
	sf::Sprite spriteDiff(textureDiff);

	const auto path = testFolder + "/basi6a16.png";
	for (const auto& entry : std::filesystem::directory_iterator(testFolder))
	{
		const auto path = entry.path().string();

		std::println("testing: {} ", entry.path().filename().string());

		std::ifstream fileStream(path, std::ios_base::binary);

		auto imageOpt = readPng(fileStream);

		sf::Image ref;

		try
		{
			ref = sf::Image(path);
		}
		catch (std::exception e)
		{
			if (imageOpt)
			{
				std::cout << "not the same" << std::endl;
				break;
			}
			else
			{
				continue;
			}
		}

		if (!imageOpt && (ref.getSize().x != 0 || ref.getSize().y != 0))
		{
			std::cout << "not the same" << std::endl;
			break;
		}

		auto& image = *imageOpt;

		if (std::memcmp(image.getPixelsPtr(), ref.getPixelsPtr(), ref.getSize().x * ref.getSize().y * 4) != 0)
		{
			std::cout << "not the same" << std::endl;

			image.saveToFile(testFolder + "/out.png");

			texture = sf::Texture{ image };
			sprite = sf::Sprite{ texture };

			textureRef = sf::Texture{ ref };
			spriteRef = sf::Sprite{ textureRef };

			auto diffImage = ref;


			break;
		}

		//image.saveToFile(testFolder + "/out.png");
	}

	auto window = sf::RenderWindow(sf::VideoMode({32, 32}), "CMake SFML Project");

	while (window.isOpen())
	{
		while (const std::optional event = window.pollEvent())
		{
			if (event->is<sf::Event::Closed>())
			{
				window.close();
			}
		}

		window.clear(sf::Color::Yellow);

		window.draw(sprite);

		spriteRef.setPosition({ 40.f, 0.f });
		window.draw(spriteRef);

		window.display();
	}

	return 0;
}
