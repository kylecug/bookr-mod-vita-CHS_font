/*
 * Bookr % VITA: document reader for the Sony PS Vita
 * Copyright (C) 2017 Sreekara C. (pathway27 at gmail dot com)
 *
 * IS A MODIFICATION OF THE ORIGINAL
 *
 * Bookr and bookr-mod for PSP
 * Copyright (C) 2005 Carlos Carrasco Martinez (carloscm at gmail dot com),
 *               2007 Christian Payeur (christian dot payeur at gmail dot com),
 *               2009 Nguyen Chi Tam (nguyenchitam at gmail dot com),

 * AND VARIOUS OTHER FORKS.
 * See Forks in the README for more info
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <map>

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <time.h>
#include <malloc.h>

extern "C" {
	#include <mupdf/fitz.h>
}

#include "bkmudocument.h"
#include "../bkbookmark.h"
#include "../utils.h"

using namespace std;

// make sure only one document is open, do we need this?
// vita can probably do multiple.
static BKMUDocument* mudoc_singleton = nullptr;
// texture of current pixmap, TODO: generic fztexture
static vita2d_texture *texture;

BKMUDocument::BKMUDocument(string& f) : 
  filename(f), m_ctx(nullptr), m_doc(nullptr), m_page(nullptr), loadNewPage(false),
  m_pageText(nullptr), m_links(nullptr), panX(0), panY(0),
  m_curPageLoaded(false), m_current_page(0), m_fitWidth(true), m_scale(1)
{
  #ifdef DEBUG
    printf("BKMUDocument::BKMUDocument\n");
  #endif

  m_rotate = 0.0f;
  m_width = FZ_SCREEN_WIDTH;
  m_height = FZ_SCREEN_HEIGHT;

  // Initalize fitz context
  m_ctx = fz_new_context(nullptr, nullptr, FZ_STORE_DEFAULT);
  if (m_ctx)
    fz_register_document_handlers(m_ctx);
  else
    printf("MuPDF context allocation problem");

  fz_set_use_document_css(m_ctx, 1);

  // Open Document; TODO: Implement keyboard password
  fz_try(m_ctx) {
    m_doc = fz_open_document(m_ctx, f.c_str());
    if (fz_needs_password(m_ctx, m_doc)) {
      int okay = 0;
      char *password;
      char defaultValue;
      // input for password
      if (!okay)
        fz_throw(m_ctx, FZ_ERROR_GENERIC, "no pass");
    }
  } fz_catch(m_ctx) {
    fz_throw(m_ctx, FZ_ERROR_GENERIC, "opening error");
  }

  // Set page count
  fz_try(m_ctx) {
    m_pages = fz_count_pages(m_ctx, m_doc);
  } fz_catch(m_ctx) {
    fz_throw(m_ctx, FZ_ERROR_GENERIC, "page_count error");
  }
}

BKMUDocument::~BKMUDocument() {
  #ifdef DEBUG
    printf("BKMUDocument::~BKMUDocument\n");
  #endif
  
  saveLastView();
  mudoc_singleton = nullptr;
  fz_drop_pixmap(m_ctx, m_pix);
  fz_drop_document(m_ctx, m_doc);
  fz_drop_context(m_ctx);
}

BKMUDocument* BKMUDocument::create(string& file) {
  #ifdef DEBUG
    printf("BKMUDocument::create\n");
  #endif
  
	if (mudoc_singleton) {
		printf("cannot open more than 1 pdf at the same time\n");
		return mudoc_singleton;
	}

	BKMUDocument* b = new BKMUDocument(file);
	mudoc_singleton = b;


  b->redrawBuffer(false);
  return b;
}

int BKMUDocument::updateContent() {
  if (loadNewPage) {
    redrawBuffer();

    panY = 0;
    loadNewPage = false;
    char t[256];

		snprintf(t, 256, "Page %d of %d", m_current_page + 1, m_pages);
		setBanner(t);
    
		return BK_CMD_MARK_DIRTY;
  }
	return 0;
}

int BKMUDocument::resume() {
	// mupdf leaves open file descriptors around. they don't survive a suspend.
  // Is this still the case?
	// pdfReopenFile();
	return 0;
}

void BKMUDocument::renderContent() {
  #ifdef DEBUG_RENDER
    printf("BKMUDocument::renderContent %i / %i\n", m_current_page, m_pages);
  #endif

  FZScreen::clear(0xefefef, FZ_COLOR_BUFFER);
  // char text[100];
  // sprintf(text, "Hi from PDF::RenderCOntent %d/%d", m_page_number, m_page_count);
  // FZScreen::drawText(100, 100, RGBA8(0,0,0,255), 1.0f, text);
  //
  // sprintf(text, "Pixmap info h:%d w:%d", m_pix->h, m_pix->w);

	// TODO: convert to texture
  // printf("BKMUDocument::renderContent -- pre pixel\n");
  vita2d_draw_texture(texture, panX, panY);
  // printf("BKMUDocument::renderContent -- post pixel\n");

	// TODO: Show Page Error, don"t draw texture then.
	// if (pageError) {
	// 	texUI->bindForDisplay();
	// 	FZScreen::ambientColor(0xf06060ff);
	// 	drawRect(0, 126, 480, 26, 6, 31, 1);
	// 	fontBig->bindForDisplay();
	// 	FZScreen::ambientColor(0xff222222);
	// 	FZScreen::drawText(380, 400, RGBA8(0,0,0,255), 1.0f, "Error in page...");
	// }
}

int BKMUDocument::getTotalPages() {
	return m_pages;
}

int BKMUDocument::getCurrentPage() {
	return m_current_page;
}

int BKMUDocument::setCurrentPage(int page_number) {
  if (page_number < 0 || page_number >= m_pages)
    // TODO(UI): Some visual notice of start or end
    return 1;
  else {
    loadNewPage = true;
    m_current_page = page_number;

		char t[256];
		snprintf(t, 256, "Loading page %d", m_current_page + 1);
		setBanner(t);
		
		return 0;
  }
}

// Draws current page into texture using pixmap
bool BKMUDocument::redrawBuffer(bool setSpeed) {
  #ifdef DEBUG
    printf("BKMUDocument::redrawBuffer\n");
  #endif
  
	// fz_scale(&m_transform, m_scale / 72, m_scale / 72);
  // fz_pre_rotate(&m_transform, m_rotate);
  
  fz_drop_stext_page(m_ctx, m_pageText);
  m_pageText = nullptr;
  fz_drop_link(m_ctx, m_links);
  m_links = nullptr;
  fz_drop_page(m_ctx, m_page);
  m_page = nullptr;

  m_page = fz_load_page(m_ctx, m_doc, m_current_page);
  m_links = fz_load_links(m_ctx, m_page);
  m_pageText = fz_new_stext_page_from_page(m_ctx, m_page, nullptr);

  // bounds for inital window size
  fz_bound_page(m_ctx, m_page, &m_bounds);
  if (m_fitWidth)
    m_scale = m_width / (m_bounds.x1 - m_bounds.x0);

  fz_scale(&m_transform, m_scale, m_scale);
  fz_pre_rotate(&m_transform, m_rotate);
  fz_transform_rect(&m_bounds, &m_transform);

  // TODO: Is display list or bbox better?
  fz_annot *annot;
	fz_try(m_ctx)
		m_pix = fz_new_pixmap_from_page_contents(m_ctx, m_page, &m_transform, fz_device_rgb(m_ctx), 0);
	fz_catch(m_ctx) {
		printf("cannot render page: %s\n", fz_caught_message(m_ctx));
	}

  // Crashes due to GPU memory use without this.
  vita2d_free_texture(texture);
  texture = _vita2d_load_pixmap_generic(m_pix);

  fz_drop_pixmap(m_ctx, m_pix);

  // load annotations

  #ifdef DEBUG
    printf("BKMUDocument::redrawBuffer - end\n");
  #endif

	return true;
}

bool BKMUDocument::isMUDocument(string& file) {
  // Read First 4 bytes
  char header[4];
	memset((void*)header, 0, 4);
	FILE* f = fopen(file.c_str(), "r");
  const char* ext = get_ext(file.c_str());
	if( !f )
		return false;
	fread(header, 4, 1, f);
	fclose(f);

  // TODO: get libmagic or something?
  // Trusting the user for now...
	return ((header[0] == 0x25 && header[1] == 0x50 && header[2] == 0x44 && header[3] == 0x46) ||
          (strcmp(ext, ".xps") == 0) ||
          (strcmp(ext, ".svg") == 0) ||
          (strcmp(ext, ".cbz") == 0) ||
          (strcmp(ext, ".img") == 0) ||
          (strcmp(ext, ".tiff") == 0) ||
          (strcmp(ext, ".htm") == 0) ||
          (strcmp(ext, ".html") == 0) ||
          (strcmp(ext, ".epub") == 0) ||
          (strcmp(ext, ".fb2") == 0)
  );
}

void BKMUDocument::getFileName(string& s) {
  s = filename;
}

bool BKMUDocument::isPaginated() {
  return true;
}

// TODO: Move this to bkuser.
static float speed = 0.5f;
int BKMUDocument::pan(int x, int y) {
  #ifdef DEBUG_BUTTONS
    printf("INPUT x %i y %i\n", x, y);
  #endif
  
  if (abs(x) <= FZ_ANALOG_THRESHOLD &&
      abs(y) <= FZ_ANALOG_THRESHOLD)
	return 0;

  // TODO: Choose invert analog settings
  // if settings.invertAnalog
  // x = -x

  if (abs(x) > FZ_ANALOG_THRESHOLD)
    panX -= x/10;
    //panX += speed * (x/FZ_ANALOG_THRESHOLD);

  if (abs(y) > FZ_ANALOG_THRESHOLD)
    panY -= y/10;
	  //panY += speed * (y/FZ_ANALOG_THRESHOLD);

  #ifdef DEBUG_BUTTONS
    printf("OUTPUT x %i y %i\n", panX, panY);
  #endif

	return BK_CMD_MARK_DIRTY;
}

bool BKMUDocument::isBookmarkable() {
  return true;
}

// -------------------- NOT IMPLEMENTED YET --------------------

bool BKMUDocument::isZoomable() {
  return false;
}

void BKMUDocument::getZoomLevels(vector<BKDocument::ZoomLevel>& v) {}

int BKMUDocument::getCurrentZoomLevel() {
  return 0;
}

int BKMUDocument::setZoomLevel(int new_zoom_level) {
  return 0;
}

bool BKMUDocument::hasZoomToFit() {
  return false;
}

int BKMUDocument::setZoomToFitWidth() {
  return 0;
}

int BKMUDocument::setZoomToFitHeight() {
  return 0;
}

int BKMUDocument::screenUp() {
  panY += 250;
}

int BKMUDocument::screenDown() {
  panY -= 250;
}

int BKMUDocument::screenLeft() {
  return 0;
}

int BKMUDocument::screenRight() {
  return 0;
}

bool BKMUDocument::isRotable() {
  return false;
}

int BKMUDocument::getRotation() {
  return 0;
}

int BKMUDocument::setRotation(int r, bool bForce) {
  return 0;
}


void BKMUDocument::getBookmarkPosition(map<string, int>& m) {
  m["page"] = m_current_page;
  
  m["panX"] = panX;
  m["panY"] = panY;
}

int BKMUDocument::setBookmarkPosition(map<string, int>& m) {
  #ifdef DEBUG
    printf("setBookmarkPosition: page %i", m["page"]);
  #endif
  setCurrentPage(m["page"]);

  panX = m["panX"];
  panY = m["panY"];

  return BK_CMD_MARK_DIRTY;
}

void BKMUDocument::getTitle(string&) {}
void BKMUDocument::getType(string&) {}