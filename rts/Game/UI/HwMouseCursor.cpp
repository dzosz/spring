/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#include "System/Platform/Win/win32.h"
#include "System/TypeToStr.h"
#include "Rendering/GlobalRendering.h"

#if defined(__APPLE__) || defined(HEADLESS)
	// FIXME: no hardware cursor support for macs
#elif defined(_WIN32)
	#include <windows.h>
	#include "System/Input/MouseInput.h"
	typedef unsigned char byte;
#else
	#include <X11/Xcursor/Xcursor.h>
	#include <SDL_syswm.h>
    #include <wayland-cursor.h>
    #include <wayland-client.h>
    #include <wayland-server-core.h>
    #include <sys/mman.h>
#include <unistd.h>
#include <sys/types.h>
#endif

#include "HwMouseCursor.h"

#if !defined(__APPLE__) && !defined(HEADLESS)

#include "System/bitops.h"
#include "System/Log/ILog.h"
#include "System/SpringMath.h"

#include <SDL_config.h>
#include <SDL_syswm.h>
#include <SDL_mouse.h>
#include <SDL_events.h>
#endif

#include <cstring> // memset

//////////////////////////////////////////////////////////////////////
// Platform dependent classes
//////////////////////////////////////////////////////////////////////

#if defined(__APPLE__) || defined(HEADLESS)
class HardwareCursorApple: public IHardwareCursor {
public:
	void PushImage(int xsize, int ysize, const void* mem) override {}
	void PushFrame(int index, float delay) override {}
	void SetDelay(float delay) override {}
	void SetHotSpot(CMouseCursor::HotSpot hs) override {}
	void Finish() override {}

	bool NeedsYFlip() const override { return false; }
	bool IsValid() const override { return false; }

	void Init(CMouseCursor::HotSpot hs) override {}
	void Kill() override {}
	void Bind() override {}
};


#elif defined(_WIN32)


class HardwareCursorWindows: public IHardwareCursor {
public:
	void PushImage(int xsize, int ysize, const void* mem) override;
	void PushFrame(int index, float delay) override;
	void SetDelay(float delay) override;
	void SetHotSpot(CMouseCursor::HotSpot hs) override { hotSpot = hs; }
	void Finish() override;

	bool NeedsYFlip() const override { return true; }
	bool IsValid() const override { return (cursor != nullptr); }

	void Init(CMouseCursor::HotSpot hs) override;
	void Kill() override;
	void Bind() override;

private:
	#pragma pack(push, 1)
	struct CursorDirectoryHeader {
		byte xsize;
		byte ysize;
		byte ncolors;
		byte reserved1;
		short hotx;
		short hoty;
		DWORD size;
		DWORD offset;
	};

	struct CursorInfoHeader {
		DWORD size;
		DWORD width;
		DWORD height;
		WORD  planes;
		WORD  bpp;
		DWORD res1;
		DWORD res2;
		DWORD res3;
		DWORD res4;
		DWORD res5;
		DWORD res6;
	};

	struct AnihStructure {
		DWORD size;
		DWORD images;
		DWORD frames;
		DWORD width;
		DWORD height;
		DWORD bpp;
		DWORD planes;
		DWORD rate;
		DWORD flags;
	};
	#pragma pack(pop)

	struct ImageData {
		unsigned char* data;
		int width;
		int height;
	};

	void buildIco(unsigned char* dst, ImageData& image);
	void resizeImage(ImageData* image, int new_x, int new_y);

private:
	HCURSOR cursor = nullptr;
	CMouseCursor::HotSpot hotSpot = CMouseCursor::Center;

	int xmaxsize = 0;
	int ymaxsize = 0;
	short hotx = 0;
	short hoty = 0;

	byte imageCount = 0;

	std::vector<ImageData> icons;
	std::vector<byte>  frames;
	std::vector<int>   framerates;
};


#else


// X11/Wayland
class HardwareCursorX11: public IHardwareCursor {
public:
	void PushImage(int xsize, int ysize, const void* mem) override;
	void PushFrame(int index, float delay) override;
	void SetDelay(float delay) override;
	void SetHotSpot(CMouseCursor::HotSpot hs) override { hotSpot = hs; }
	void Finish() override;

	bool NeedsYFlip() const override { return false; }
    bool IsValid() const override { return (cursor != 0); }

	void Init(CMouseCursor::HotSpot hs) override;
	void Kill() override;
	void Bind() override;

public:
	void resizeImage(XcursorImage*& image, const int new_x, const int new_y);

public:
	Cursor cursor = 0;
	CMouseCursor::HotSpot hotSpot = CMouseCursor::Center;

	int xmaxsize = 0;
	int ymaxsize = 0;

	SDL_SYSWM_TYPE subsystem;

	std::vector<XcursorImage*> cimages;
};

static void registry_global_remove(void *data, struct wl_registry *wl_registry, uint32_t name)
{
}

static void registry_global(void *data, struct wl_registry *wl_registry,
        uint32_t name, const char *interface, uint32_t version);

static const struct wl_registry_listener registry_listener = {
    .global = registry_global,
    .global_remove = registry_global_remove,
};

static void
wl_buffer_release(void *data, struct wl_buffer *wl_buffer)
{
    /* Sent by the compositor when it's no longer using this buffer */
    // wl_buffer_destroy(wl_buffer);
    // LOG("XYZ buffer release %p", wl_buffer);
}

static const struct wl_buffer_listener wl_buffer_listener = {
    .release = wl_buffer_release,
};



void pointer_enter_handler ( void *data, struct wl_pointer *pointer, uint32_t serial, struct wl_surface *surface, wl_fixed_t x, wl_fixed_t y);

void pointer_leave_handler ( void *data, struct wl_pointer *pointer, uint32_t serial, struct wl_surface *surface) {
    LOG("XYZ LEAVE %p ", serial);
}

void pointer_motion_handler ( void *data, struct wl_pointer *pointer, uint32_t time, wl_fixed_t x, wl_fixed_t y) { }

void pointer_button_handler ( void *data, struct wl_pointer *pointer, uint32_t serial, uint32_t time, uint32_t button, uint32_t state) {}

void pointer_axis_handler ( void *data, struct wl_pointer *pointer, uint32_t time, uint32_t axis, wl_fixed_t value) { }

void pointer_handle_frame(void *data, struct wl_pointer *pointer) {}
void pointer_handle_axis_source(void *data, struct wl_pointer *pointer, uint32_t axis_source) {}
void pointer_handle_axis_stop(void *data, struct wl_pointer *pointer, uint32_t time, uint32_t axis)  {}
void pointer_handle_axis_discrete(void *data, struct wl_pointer *pointer, uint32_t axis, int32_t discrete) {}

static void seat_handle_capabilities(void *data, struct wl_seat *seat, uint32_t caps);

static void seat_handle_name(void *data, struct wl_seat *wl_seat, const char *name) {}

static const struct wl_seat_listener seat_listener = {
     seat_handle_capabilities,
     seat_handle_name,           // Version 2
 };

 struct wl_pointer_listener pointer_listener = {
    .enter = pointer_enter_handler,
    .leave = pointer_leave_handler,
    .motion = pointer_motion_handler,
    .button = pointer_button_handler,
    .axis = pointer_axis_handler,
    pointer_handle_frame,           // Version 5
     pointer_handle_axis_source,     // Version 5
     pointer_handle_axis_stop,       // Version 5
     pointer_handle_axis_discrete,
};

struct cursor_image {
    wl_cursor_image image;
    //struct wl_cursor_theme *theme;
    wl_buffer *buffer;
    uint8_t* data;
    uint32_t offset; // data offset of this image in the shm pool
    float delay;
};


struct WaylandHandler {
    void preallocate(cursor_image& cur) {
        auto stride = cur.image.width * 4;
        auto size = cur.image.width * cur.image.height * 4;

        if (ftruncate(this->fd, this->data_size + size)) {
            LOG("XYZ ERR");
            throw;
        }

        cur.data = static_cast<uint8_t*>(mmap(NULL, data_size + size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0));
        if (cur.data == MAP_FAILED) {
            LOG("XYZ ERR");
            throw;
        }

        cur.offset = data_size;
        LOG("XYZ preallocate %ld addr=%p", size, cur.data);
        this->data_size += size;
    }

    void create_shared_image(cursor_image& cur) {
        auto size = cur.image.width * cur.image.height * 4;
        auto stride = cur.image.width * 4;

        LOG("XYZ create_shared_image %ld addr=%p", size, cur.data);

        auto* pool = wl_shm_create_pool(this->shm, this->fd, data_size);
        cur.buffer = wl_shm_pool_create_buffer(pool, cur.offset,
                      cur.image.width, cur.image.height, stride,
                      WL_SHM_FORMAT_ARGB8888);
        wl_shm_pool_destroy(pool);
        // wl_buffer_add_listener(cur.buffer, &wl_buffer_listener, NULL);
        munmap(cur.data, data_size);
        cur.data = 0;
    }

    void show_cursor(cursor_image& cur) {
        //LOG("XYZ show_cursor buf=%p", cur.buffer);
        wl_surface_attach(this->surface, cur.buffer, 0, 0);
        wl_surface_damage(surface, 0, 0, cur.image.width, cur.image.height);
        wl_surface_commit(this->surface);
        wl_pointer_set_cursor(this->pointer, this->serial, this->surface, 0, 0);
        // LOG("XYZ show_cursor END", cur.buffer);
    }

    int fd = 0;
    // uint8_t* data = nullptr;
    uint32_t data_size = 0;

    wl_display *display=0;
    wl_compositor *compositor=0;
    wl_shm *shm=0;
    wl_seat *seat=0;
    //wl_cursor_theme *theme;
    wl_pointer* pointer=0;
    wl_registry* registry=0;
    wl_surface *surface=0;
    uint32_t serial=0;

    static WaylandHandler& getInstance() {
        static WaylandHandler instance = WaylandHandler{};
        return instance;
    }
private:
    WaylandHandler() {

        LOG("XYZ WaylandHandler()");
        //this->display = wl_display_connect(NULL);
        SDL_SysWMinfo info;
        SDL_VERSION(&info.version);

        if (SDL_GetWindowWMInfo(globalRendering->GetWindow(0), &info)) {
            this->display = info.info.wl.display;
        } else {
            LOG("XYZ can't get wayland");
             throw;
         }
        this->registry = wl_display_get_registry(this->display);
        wl_registry_add_listener(this->registry, &registry_listener, this);
        wl_display_roundtrip(this->display);

        wl_seat_add_listener(this->seat, &seat_listener, this);
        wl_seat_set_user_data(this->seat, this);

        //this->pointer = wl_seat_get_pointer(this->seat);
        wl_pointer_add_listener(this->pointer, &pointer_listener, this);

        this->surface = wl_compositor_create_surface(this->compositor);
        LOG("XYZ WaylandHandler() done");

        static auto fd = memfd_create("test_spring123", 0);
        this->fd = fd;
        //this->fd = shm_open("test_spring", O_RDWR
    }
};

void pointer_enter_handler ( void *data, struct wl_pointer *pointer, uint32_t serial, struct wl_surface *surface, wl_fixed_t x, wl_fixed_t y) {
    // !mouse->offscreen
    LOG("XYZ pointer_enter_handler %p", serial);
    auto* handler = static_cast<WaylandHandler*>(data);
    handler->serial=serial;
    int hotx = 0; // TODO
    int hoty = 0; // TODO
    // wl_pointer_set_cursor(handler->pointer, serial, handler->surface, hotx, hoty);
    LOG("XYZ pointer_enter_handler DONE");
}

static void seat_handle_capabilities(void *data, struct wl_seat *seat, uint32_t caps) {
    auto* handler = static_cast<WaylandHandler*>(data);
    if ((caps & WL_SEAT_CAPABILITY_POINTER) && !handler->pointer) {
        handler->pointer = wl_seat_get_pointer(seat);
    }
}

class HardwareCursorWayland : public IHardwareCursor {
public:
    HardwareCursorWayland() : whandler(WaylandHandler::getInstance()) {}

    void PushImage(int xsize, int ysize, const void* mem) override {
        return;
        auto cur = cursor_image{ wl_cursor_image {(uint32_t)xsize, (uint32_t)ysize, 0, 0, 0 }, nullptr, nullptr, 0, CMouseCursor::DEF_FRAME_LENGTH};
        whandler.preallocate(cur);
        uint8_t* dst = cur.data + cur.offset;
        const uint8_t* src = static_cast<const uint8_t*>(mem);
        const uint8_t* end = src + xsize * ysize * 4;

        do {
            dst[0] = src[2];      // B
            dst[1] = src[1];      // G
            dst[2] = src[0];      // R
            dst[3] = src[3];      // A
            dst += 4;
            src += 4;
        } while (src < end);

        whandler.create_shared_image(cur);
        wimages.push_back(cur);
        LOG("XYZ DONE PushImage");
    }

	void PushFrame(int index, float delay) override {
        if (index >= wimages.size()) {
            return;
        }

        auto& elem = wimages[index];
        if (elem.delay != delay) {
            this->wimages.push_back(elem);
            //this->PushImage(elem.image.width, elem.image.height, elem.data);
            this->SetDelay(delay);
        }

    }

    void Update(float animTime) override {
        LOG("Update %f of %f", animTime, total_delay);
        animTime = fmod(animTime, this->total_delay);
        float total = 0;
        auto elem = wimages.begin();
        while (elem != wimages.end()) {
            total += elem->delay;
            if (total > animTime) {
                whandler.show_cursor(*elem);
                break;
            }
            std::advance(elem, 1);
        }

    }


    void SetDelay(float delay) override {
        if (!wimages.empty()) {
            wimages.back().delay = delay;
        }
    }
    void SetHotSpot(CMouseCursor::HotSpot hs) override {
        hotSpot = hs;
    }
    void Finish() override {
        LOG("XYZ FINISH");        
        for (auto &c : wimages) {

            this->total_delay += c.delay;
            // c.image.hotspot_x = (hotSpot == CMouseCursor::TopLeft) ? 0 : xmaxsize / 2;
            // c.image.hotspot_y == CMouseCursor::TopLeft) ? 0 : ymaxsize / 2;
            // LOG("XYZ fd=%i data=%p size=%u data_size=%zu RESIZED=%zu", fd, cur.data, size, this->data_size, this->data_size + size);

        }
        //cursor = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_HAND);
        //if (!cursor) {
        //    LOG_L(L_WARNING, "[%s::%s] Can't create Wayland HWCursor", spring::TypeToCStr<decltype(*this)>(), __func__);
        //    return;
        //}
    }

	bool NeedsYFlip() const override { return false; }
    bool IsValid() const override { return !wimages.empty();}

	void Init(CMouseCursor::HotSpot hs) override {
         LOG_L(L_WARNING, "XYZ Init");
    }

	void Kill() override {
        LOG_L(L_WARNING, "XYZ Kill");
        this->wimages.clear();
        //if (cursor)
        //    SDL_FreeCursor(cursor);
        //if (surface)
        //    SDL_FreeSurface(surface);

        //cursor = nullptr;
        //surface = nullptr;
    }

    void Bind() override {
        LOG_L(L_WARNING, "XYZ Bind %i", wimages.size());
        if (wimages.size()) {
            SDL_ShowCursor(SDL_ENABLE);
            whandler.show_cursor(wimages[0]);
            LOG_L(L_WARNING, "XYZ Bind END");
        }
    }

public:
    std::vector<cursor_image> wimages;
    WaylandHandler& whandler;
    CMouseCursor::HotSpot hotSpot;
    float total_delay=0;
};

static void registry_global(void *data, struct wl_registry *wl_registry,
        uint32_t name, const char *interface, uint32_t version)
{
    auto* state = static_cast<WaylandHandler*>(data);
    if (strcmp(interface, wl_shm_interface.name) == 0) {
        state->shm = static_cast<wl_shm*>(wl_registry_bind(
                wl_registry, name, &wl_shm_interface, version));
    } else if (strcmp(interface, wl_compositor_interface.name) == 0) {
        state->compositor = static_cast<wl_compositor*>(wl_registry_bind(
                wl_registry, name, &wl_compositor_interface, version));
    } else if (strcmp(interface, wl_seat_interface.name) == 0) {
        state->seat = static_cast<wl_seat*>(wl_registry_bind(wl_registry, name,
            &wl_seat_interface, version));
    }
    /*
    else if (strcmp(interface, wl_pointer_interface.name) == 0) {
        state->pointer = static_cast<wl_pointer*>(wl_registry_bind(wl_registry, name, &wl_seat_interface, 1));
    } */
}



#endif

IHardwareCursor* IHardwareCursor::Alloc(void* mem) {
#if defined(__APPLE__) || defined(HEADLESS)
	static_assert(sizeof(HardwareCursorApple  ) <= CMouseCursor::HWC_MEM_SIZE, "");
	return (new (mem) HardwareCursorApple());
#elif defined (_WIN32)
	static_assert(sizeof(HardwareCursorWindows) <= CMouseCursor::HWC_MEM_SIZE, "");
	return (new (mem) HardwareCursorWindows());
#else //LINUX
	static_assert(sizeof(HardwareCursorX11    ) <= CMouseCursor::HWC_MEM_SIZE, "");
	static_assert(sizeof(HardwareCursorWayland) <= CMouseCursor::HWC_MEM_SIZE, "");

	SDL_SysWMinfo info;
	SDL_VERSION(&info.version);

	if (SDL_GetWindowWMInfo(globalRendering->GetWindow(0), &info)) {
		switch (info.subsystem)
		{
        case SDL_SYSWM_WAYLAND:
            return (new (mem) HardwareCursorWayland());
		case SDL_SYSWM_X11:
			return (new (mem) HardwareCursorX11());
		default: {
			assert(false);
		} break;
		}
	}
#endif

	return nullptr;
}

void IHardwareCursor::Free(IHardwareCursor* hwc) {
	hwc->~IHardwareCursor();
	memset(reinterpret_cast<char*>(hwc), 0, CMouseCursor::HWC_MEM_SIZE);
}




//////////////////////////////////////////////////////////////////////
// Implementations
//////////////////////////////////////////////////////////////////////


#if defined(__APPLE__) || defined(HEADLESS)


#elif defined(_WIN32)


void HardwareCursorWindows::Init(CMouseCursor::HotSpot hs)
{
	cursor = nullptr;
	hotSpot = hs;

	imageCount = 0;

	xmaxsize = 0;
	ymaxsize = 0;

	hotx = 0;
	hoty = 0;
}

void HardwareCursorWindows::Kill()
{
	if (cursor != nullptr)
		DestroyCursor(cursor);

	for (ImageData& image: icons)
		delete[] image.data;

	icons.clear();
}


void HardwareCursorWindows::PushImage(int xsize, int ysize, const void* mem)
{
	xmaxsize = std::max(xmaxsize, xsize);
	ymaxsize = std::max(ymaxsize, ysize);

	icons.emplace_back();
	ImageData& icon = icons.back();

	icon.data = new unsigned char[xsize * ysize * 4];
	icon.width = xsize;
	icon.height = ysize;
	memcpy(icon.data, mem, xsize * ysize * 4);

	frames.push_back(imageCount++);
	framerates.push_back(std::max(int(CMouseCursor::DEF_FRAME_LENGTH * 60.0f), 1));
}

void HardwareCursorWindows::SetDelay(float delay)
{
	framerates.back() = std::max(int(delay * 60.0f), 1);
}

void HardwareCursorWindows::PushFrame(int index, float delay)
{
	if (index >= imageCount)
		return;

	frames.push_back((byte)index);
	framerates.push_back(std::max((int)(delay * 60.0f), 1));
}

void HardwareCursorWindows::resizeImage(ImageData* image, int new_x, int new_y)
{
	if (image->width == new_x && image->height == new_y)
		return;

	unsigned char* newdata = new unsigned char[new_x * new_y * 4];
	memset(newdata, 0, new_x * new_y * 4);

	for (int y = 0; y < image->height; ++y)
		for (int x = 0; x < image->width; ++x)
			for (int v = 0; v < 4; ++v)
				newdata[((y + (new_y - image->height)) * new_x + x) * 4 + v] = image->data[(y * image->width + x) * 4 + v];

	delete[] image->data;
	image->data = newdata;

	image->width  = new_x;
	image->height = new_y;
}

void HardwareCursorWindows::buildIco(unsigned char* dst, ImageData& image)
{
	const int xsize = image.width;
	const int ysize = image.height;

	{
		//small header needed in .ani
		strcpy((char*) dst, "icon");
		dst += 4;
		DWORD* cursize = (DWORD*) &dst[0];
		cursize[0] = 3 * sizeof(WORD) + sizeof(CursorDirectoryHeader) + sizeof(CursorInfoHeader) + (xsize * ysize * 4) + (xsize * ysize / 8);
		dst += 4;
	}

	////////////////////////////////////////////////////////////////////////////////////////////////
	// the following code writes a fully working .cur file in the memory (in a .ani container)

	{
		// file header
		int i = 0;
		WORD* header = (WORD*) &dst[0];
		header[i++] = 0;		// reserved
		header[i++] = 2;		// is cursor
		header[i++] = 1;		// number of cursors in this file
		dst += (3 * sizeof(WORD));
	}
	{
		CursorDirectoryHeader curHeader;
		memset(&curHeader, 0, sizeof(CursorDirectoryHeader));
		curHeader.xsize  = (byte) xsize;
		curHeader.ysize  = (byte) ysize;
		curHeader.hotx   = hotx;
		curHeader.hoty   = hoty;
		curHeader.size   = 40 + xsize * ysize * 4 + xsize * ysize / 8; // size of the bmp data (infoheader 40byte, 32bit color, 1bit mask)
		curHeader.offset = 22;                                         // offset where the infoHeader starts
		memcpy(dst, &curHeader, sizeof(CursorDirectoryHeader));
		dst += sizeof(CursorDirectoryHeader);
	}
	{
		CursorInfoHeader infoHeader;
		memset(&infoHeader, 0, sizeof(CursorInfoHeader));
		infoHeader.size   = 40;		//size of the infoHeader
		infoHeader.width  = xsize;		//cursor width
		infoHeader.height = ysize*2;		//cursor height + mask height
		infoHeader.planes = 1;		//number of color planes
		infoHeader.bpp    = 32;		//bits per pixel
		memcpy(dst, &infoHeader, sizeof(CursorInfoHeader));
		dst += sizeof(CursorInfoHeader);
	}

	//copy colormap
	unsigned char* src = image.data;
	unsigned char* end = src + xsize * ysize * 4;

	do {
		if (src[3] == 0) {
			dst[0] = dst[1] = dst[2] = dst[3] = 0;
		} else {
			dst[0] = src[2];      // B
			dst[1] = src[1];      // G
			dst[2] = src[0];      // R
			dst[3] = src[3];      // A
		}
		dst += 4;
		src += 4;
	} while (src < end);

	// create mask
	src -= xsize*ysize*4;
	int b = 0;
	do {
		dst[0] = 0x00;
		for (b = 0; b < 8; b++) {
			if (src[3] == 0) //alpha greater zero?
				dst[0] |= (128 >> b);
			src += 4;
		}
		dst++;
	} while (src < end);

	#if 0
	char fname[256];
	SNPRINTF(fname, sizeof(fname), "mycursor%d.cur", ++savedcount);
	FILE* pFile = fopen(fname, "wb" );
	fwrite(curs.back(), 1, curmem - curs.back(), pFile);
	fclose(pFile);
	#endif
}


static inline int GetBestCursorSize(const int minSize)
{
	constexpr int stdSizes[] = {/*16,*/ 32, 48, 64, 96, 128};
	for (const int s: stdSizes)
		if (s >= minSize)
			return s;

	return next_power_of_2(minSize);
}


void HardwareCursorWindows::Finish()
{
	if (frames.empty())
		return;

	hotx = (hotSpot == CMouseCursor::TopLeft) ? 0 : (short)xmaxsize / 2;
	hoty = (hotSpot == CMouseCursor::TopLeft) ? 0 : (short)ymaxsize / 2;

	const int squaresize = GetBestCursorSize(std::max(xmaxsize, ymaxsize));

	for (ImageData& id: icons)
		resizeImage(&id, squaresize, squaresize);

	const int riffsize  = 32 + sizeof(AnihStructure) + (frames.size() + 2) * 2 * sizeof(DWORD);
	const int iconssize = icons.size() * (2 * sizeof(DWORD) + 3 * sizeof(WORD) +
					       sizeof(CursorDirectoryHeader) +
					       sizeof(CursorInfoHeader) +
					       squaresize * squaresize * 4 +
					       squaresize * squaresize / 8);
	const int totalsize = riffsize + iconssize;


	static std::vector<unsigned char> mem;

	mem.clear();
	mem.resize(totalsize, 0);


	unsigned char* curmem = mem.data();
	DWORD* dwmem = nullptr;

	{
		//write RIFF header
		strcpy((char*) curmem, "RIFF");   curmem += 4;
		dwmem = reinterpret_cast<DWORD*>(&curmem[0]);
		dwmem[0] = totalsize - 8;         curmem += 4; // filesize
		strcpy((char*) curmem, "ACON");   curmem += 4;
	}
	{
		//Anih header
		strcpy((char*) curmem, "anih");
		curmem += 4;
		curmem[0] = 36;
		curmem[1] = curmem[2] = curmem[3] = 0;
		curmem += 4;

		AnihStructure anih;
		memset(&anih, 0, sizeof(AnihStructure));
		anih.size   = 36;                   // anih structure size
		anih.images = imageCount;           // number of images
		anih.frames = framerates.size();    // number of frames
		anih.flags  = 0x3L;                 // using seq structure and .cur format for saving bmp data
		memcpy(curmem, &anih, sizeof(AnihStructure));
		curmem += sizeof(AnihStructure);
	}
	{
		//LIST + icons
		strcpy((char*) curmem, "LIST");   curmem += 4;
		dwmem = reinterpret_cast<DWORD*>(&curmem[0]);
		dwmem[0] = iconssize + 4;         curmem += 4;
		strcpy((char*) curmem, "fram");   curmem += 4;

		for (ImageData& id: icons) {
			buildIco(curmem, id);
			curmem += (2 * sizeof(DWORD) + 3 * sizeof(WORD));
			curmem += (sizeof(CursorDirectoryHeader) + sizeof(CursorInfoHeader));
			curmem += (squaresize * squaresize * 4 + squaresize * squaresize / 8);
		}
	}
	{
		//SEQ header
		strcpy((char*) curmem, "seq ");
		curmem += 4;
		DWORD* seq = (DWORD*) &curmem[0];
		seq[0] = frames.size() * sizeof(DWORD);
		seq++;
		for (size_t i = 0; i < frames.size(); i++)
			seq[i] = frames.at(i);
		curmem += (frames.size() + 1) * sizeof(DWORD);
	}
	{
		//RATE header
		strcpy((char*) curmem, "rate");
		curmem += 4;
		DWORD* rate = (DWORD*) &curmem[0];
		rate[0] = framerates.size() * sizeof(DWORD);
		rate++;
		for (size_t i = 0; i < framerates.size(); i++)
			rate[i] = framerates.at(i);
		curmem += (framerates.size() + 1) * sizeof(DWORD);
	}

	#if 0
	char fname[256];
	SNPRINTF(fname, sizeof(fname), "cursors/mycursor%d.ani", ++savedcount);
	FILE* pFile = fopen( fname , "wb");
	fwrite(mem, 1, curmem - mem, pFile);
	fclose(pFile);
	#endif

	cursor = (HCURSOR)CreateIconFromResourceEx((PBYTE) mem.data(), totalsize, FALSE, 0x00030000, squaresize, squaresize, 0);

	for (ImageData& id: icons)
		delete[] id.data;

	icons.clear();
}

void HardwareCursorWindows::Bind()
{
	#if 0
	SDL_SysWMinfo info;
	SDL_VERSION(&info.version);

	if (!SDL_GetWMInfo(&info)) {
		LOG_L(L_ERROR, "[%s::%s] SDL error: can't get window handle", spring::TypeToCStr<decltype(*this)>(), __func__);
		return;
	}

	// SDL doesn't let us use it
	SetClassLong(info.window, GCL_HCURSOR, (LONG) cursor);
	#endif

	SDL_ShowCursor(SDL_ENABLE);
    SetCursor(cursor);
	mouseInput->SetWMMouseCursor(cursor);
}


#else


void HardwareCursorX11::Init(CMouseCursor::HotSpot hs)
{
	cursor   = 0;
	hotSpot  = hs;
	xmaxsize = 0;
	ymaxsize = 0;
}

void HardwareCursorX11::Kill()
{
	for (XcursorImage* img: cimages)
		XcursorImageDestroy(img);

	cimages.clear();

	if (cursor != 0) {
		SDL_SysWMinfo info;
		SDL_VERSION(&info.version);

		if (!SDL_GetWindowWMInfo(globalRendering->GetWindow(0), &info)) {
			LOG_L(L_ERROR, "[%s::%s] SDL error: can't get window info", spring::TypeToCStr<decltype(*this)>(), __func__);
			return;
		}

		XFreeCursor(info.info.x11.display, cursor);
	}
}


void HardwareCursorX11::resizeImage(XcursorImage*& image, const int new_x, const int new_y)
{
	if (int(image->width) == new_x && int(image->height) == new_y)
		return;

	const int old_x = image->width;
	const int old_y = image->height;

	XcursorImage* new_image = XcursorImageCreate(new_x, new_y);
	new_image->delay = image->delay;

	const unsigned char* src = (unsigned char*)image->pixels;
	      unsigned char* dst = (unsigned char*)new_image->pixels;

	memset(dst, 0, new_x * new_y * 4);

	for (int y = 0; y < old_y; ++y)
		for (int x = 0; x < old_x; ++x)
			for (int v = 0; v < 4; ++v)
				dst[(y * new_x + x) * 4 + v] = src[(y * old_x + x) * 4 + v];

	XcursorImageDestroy(image);
	image = new_image;
}

void HardwareCursorX11::PushImage(int xsize, int ysize, const void* mem)
{
	xmaxsize = std::max(xmaxsize, xsize);
	ymaxsize = std::max(ymaxsize, ysize);

	XcursorImage* image = XcursorImageCreate(xsize, ysize);
	image->delay = (XcursorUInt) (CMouseCursor::DEF_FRAME_LENGTH * 1000.0f);

	char* dst = (char*)image->pixels;
	char* src = (char*)mem;
	char* end = src + xsize * ysize * 4;

	do {
		dst[0] = src[2];      // B
		dst[1] = src[1];      // G
		dst[2] = src[0];      // R
		dst[3] = src[3];      // A
		dst += 4;
		src += 4;
	} while (src < end);

	cimages.push_back(image);
}

void HardwareCursorX11::SetDelay(float delay)
{
	cimages.back()->delay = (XcursorUInt)(delay*1000.0f); //in millseconds
}

void HardwareCursorX11::PushFrame(int index, float delay)
{
	if (index >= int(cimages.size()))
		return;

	if (cimages[index]->delay != delay) {
		// make a copy of the existing one
		const XcursorImage* ci = cimages[index];

		PushImage(ci->width, ci->height, ci->pixels);
		SetDelay(delay);
	} else {
		cimages.push_back( cimages[index] );
	}
}

void HardwareCursorX11::Finish()
{
	if (cimages.empty())
		return;

	const int squaresize = next_power_of_2(std::max(xmaxsize, ymaxsize));

	// resize images
	for (XcursorImage*& ci: cimages)
		resizeImage(ci, squaresize, squaresize);

	XcursorImages* cis = XcursorImagesCreate(cimages.size());
	cis->nimage = cimages.size();

	for (size_t i = 0; i < cimages.size(); ++i) {
		XcursorImage* ci = cimages[i];
		ci->xhot = (hotSpot == CMouseCursor::TopLeft) ? 0 : xmaxsize / 2;
		ci->yhot = (hotSpot == CMouseCursor::TopLeft) ? 0 : ymaxsize / 2;
		cis->images[i] = ci;
	}

	SDL_SysWMinfo info;
	SDL_VERSION(&info.version);
	if (!SDL_GetWindowWMInfo(globalRendering->GetWindow(0), &info)) {
		LOG_L(L_ERROR, "[%s::%s] SDL error: can't get window info", spring::TypeToCStr<decltype(*this)>(), __func__);
		XcursorImagesDestroy(cis);
		cimages.clear();
		return;
	}

	cursor = XcursorImagesLoadCursor(info.info.x11.display, cis);
	XcursorImagesDestroy(cis);
    cimages.clear();
}

void HardwareCursorX11::Bind()
{
	SDL_SysWMinfo info;
	SDL_VERSION(&info.version);

	if (!SDL_GetWindowWMInfo(globalRendering->GetWindow(0), &info)) {
		LOG_L(L_ERROR, "[%s::%s] SDL error: can't get window info", spring::TypeToCStr<decltype(*this)>(), __func__);
		return;
	}

	// do between lock/unlock so SDL's default cursor doesn't flicker in
	SDL_ShowCursor(SDL_ENABLE);
	XDefineCursor(info.info.x11.display, info.info.x11.window, cursor);
}


#endif

