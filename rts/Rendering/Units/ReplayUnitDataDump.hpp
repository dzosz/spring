#pragma once

#include <vector>

#include "System/Misc/SpringTime.h"
#include "Sim/Misc/GlobalConstants.h"
#include "Map/ReadMap.h"
// #include "GL/gl.h"
// #include "GL/glext.h"
#include "System/Log/ILog.h"
#include "Rendering/GL/myGL.h"
#include "Rendering/GlobalRendering.h"

#include <fstream>

struct MatchFrame {
	int id;
	unsigned units;
};

struct __attribute__ ((packed)) UnitData {
	uint8_t id=0;
	uint8_t type=0;
	uint8_t team=0;
	//uint8_t player=0;
	// bool new_unit=false;
	uint16_t pos_x=0;
	uint16_t pos_y=0;
};

struct MapData {
	//uint16_t dim_x=0;
	//uint16_t dim_y=0;
	uint16_t minimap_x=0;
	uint16_t minimap_y=0;
	std::vector<uint8_t> data;
};

struct MatchDataWriter;

struct MatchData {
	const size_t framePerSec = GAME_SPEED;
	size_t sampleFramePeriod = framePerSec * 5; // in seconds

	std::vector<UnitData>   unit_data;
	std::vector<MatchFrame> frames;
	MapData mapdata;
	std::string fileName;

	MatchData(std::string name) {
		this->fileName = std::move(name);
		this->fileName.append(".matchdata");

		this->unit_data.reserve(1024 * 1024 * 5);
		this->frames.reserve(3600 * framePerSec / sampleFramePeriod);
	}

	void write_data() {
		this->get_minimap_texture();
		// LOG("XYZ write to %s", this->fileName.c_str());

		std::ofstream f(fileName, std::ios::binary);
		f << "Long Live Coil";
		f.write((const char*) &mapdata.minimap_x, sizeof(mapdata.minimap_x));
		f.write((const char*) &mapdata.minimap_y, sizeof(mapdata.minimap_y));
		f.write((const char*) mapdata.data.data(), mapdata.data.size());

		LOG("XYZ saved pixels: %u", mapdata.data.size());
		return;

		auto units_it = unit_data.begin();
		for (auto& frame : this->frames) {
			f.write((const char*)&frame.id, sizeof(frame.id));
			f.write((const char*)&frame.units, sizeof(frame.units));
			f.write((const char*)&*units_it, frame.units * sizeof(UnitData));
			std::advance(units_it, frame.units);
		}


	}

	bool active_frame(int current_frame) {
		//LOG("XYZ active_frame %i %i", current_frame, sampleFramePeriod);
		if (this->frames.size() > 2) {
			write_data();

			exit(1);
		}
		if (current_frame % sampleFramePeriod == 0) {
			this->frames.push_back({current_frame, 0});
			return true;
		}
		return false;
	}

	void add_pos(uint8_t id, uint8_t type, uint8_t team, uint16_t x, uint16_t y) {
		this->unit_data.push_back({id, type, team, x, y});
		this->frames.back().units += 1;
	}

	void get_minimap_texture() {
		auto tex = readMap->GetTexture(MAP_BASE_MINIMAP_TEX);
		// auto tex = readMap->GetMiniMapTexture();
		int2 tex_size = readMap->GetTextureSize(MAP_BASE_MINIMAP_TEX);

		auto& pixels = this->mapdata.data;
		pixels.resize(tex_size.x * tex_size.y * 4);

		/// READ THE CONTENT FROM THE FBO
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, tex);
		//glBindFramebuffer(GL_FRAMEBUFFER, tex);
		// glReadBuffer(tex);
		//glReadPixels(0, 0, tex_size.x, tex_size.y, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
		glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
		//glGetTextureSubImage(GL_TEXTURE_2D, 0, 0, 0, 0,0 ,tex_size.x, tex_size.y, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
		//glGetTextureImage(tex, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixels.size(), pixels.data());

		this->mapdata.minimap_x = tex_size.x;
		this->mapdata.minimap_y = tex_size.y;

		LOG("XYZ	 %i x %i y %i", tex, tex_size.x, tex_size.y);

		//ThreadPool::Enqueue([](const FunctionArgs& args) {
				//CBitmap bmp(pixels.data(), tex_size.x, tex_size.y);
                // bmp.ReverseYAxis();
		//auto quality=90;
		//        bmp.Save("XYZ.jpg", true, true, quality);
		 //         }, args);


	}
};

struct MatchDataWriter {
	MatchData& matchdata;

	void write() {

	}
};
