#pragma once

#include <deque>
#include <pair>
#include <vector>

#include "System/Misc/SpringTime.h"

using UnitPosition = std::pair<float, float>;

struct MatchFrame {
	std::vector<UnitPosition>;
};

struct MatchData {
	size_t sampleDelayMs = 1000;
	spring_t lastUpdate = 0;
	std::deque<MatchFrame> frames;

	MatchData() {
		get_minimap_texture();
	}

	bool create_frame() {
		if ((spring_gettime() - lastUpdate).toMilliSecsi() > sampleDelayMs) {
			frames.push_back();
			return true;
		}
	}

	void add_pos(float x, float y) {
		frames.back().emplace_back({x,y});
	}

	void get_minimap_texture() {
		auto tex = readMap->GetTexture(MAP_BASE_MINIMAP_TEX);
		int2 tex_size = readMap->GetTextureSize(MAP_BASE_MINIMAP_TEX);

		std::vector<uint8_t> pixels;
		pixels.resize(tex_size.x * tex_size.y * 4);

		/// READ THE CONTENT FROM THE FBO
		glReadBuffer(tex);
		glReadPixels(0, 0, tex_size.x, tex_size.y, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());

		//  mapDims.mapx
		//  mapDims.mapy

		//ThreadPool::Enqueue([](const FunctionArgs& args) {
                CBitmap bmp(pixels.data(), tex_size.x, tex_size.y);
                // bmp.ReverseYAxis();
		auto quality=90;
                bmp.Save("XYZ.jpg", true, true, quality);
		 //         }, args);


	}
};
