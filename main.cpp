#include <SFML/Graphics.hpp>

#include "src/png.hpp"

#include <fstream>
#include <string>
#include <string_view>
#include <print>
#include <iostream>

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

		auto imageOpt = png::readPng(fileStream);

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

		sf::Image image({ imageOpt->width, imageOpt->height }, imageOpt->data.data());

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

	auto window = sf::RenderWindow(sf::VideoMode({ 32, 32 }), "CMake SFML Project");

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
