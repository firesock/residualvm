/* ResidualVM - A 3D game interpreter
 *
 * ResidualVM is the legal property of its developers, whose names
 * are too numerous to list here. Please refer to the AUTHORS
 * file distributed with this source distribution.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 */

#include "engines/stark/formats/xmg.h"
#include "engines/stark/debug.h"
#include "engines/stark/gfx/driver.h"

#include "graphics/conversion.h"
#include "graphics/pixelformat.h"
#include "graphics/surface.h"
#include "common/stream.h"

namespace Stark {
namespace Formats {

Graphics::Surface *XMGDecoder::decode(Common::ReadStream *stream) {
	XMGDecoder dec;
	return dec.decodeImage(stream);
}

// TODO: fix color handling in BE machines?
Graphics::Surface *XMGDecoder::decodeImage(Common::ReadStream *stream) {
	_stream = stream;

	// Read the file version
	uint32 version = stream->readUint32LE();
	if (version != 3) {
		warning("Stark::XMG: File version unknown: %d", version);
	}

	// Read the transparency color (RGBA)
	_transColor = stream->readUint32LE();

	// Read the image size
	_width = stream->readUint32LE();
	_height = stream->readUint32LE();
	if (_width % 2) {
		_width++;
	}
	debugC(10, kDebugXMG, "Stark::XMG: Version=%d, TransparencyColor=0x%08x, size=%dx%d", version, _transColor, _width, _height);

	// Read the scan length
	_scanLen = stream->readUint32LE();
	if (_scanLen != 3 * _width) {
		warning("Stark::XMG: The scan length (%d) doesn't match the width bytes (%d)", _scanLen, 3 * _width);
	}
	// We're using RGBA instead of RGB, so we use our own scanlength
	_scanLen = _width;

	// Unknown
	uint32 unknown2 = stream->readUint32LE();
	debugC(kDebugUnknown, "Stark::XMG: unknown2 = %08x = %d", unknown2, unknown2);
	uint32 unknown3 = stream->readUint32LE();
	debugC(kDebugUnknown, "Stark::XMG: unknown3 = %08x = %d", unknown3, unknown3);

	// Create the destination surface
	Graphics::Surface *surface = new Graphics::Surface();
	if (!surface)
		return NULL;
	surface->create(_width, _height, Graphics::PixelFormat(4,8,8,8,8,24,16,8,0));

	_pixels = (uint32 *)surface->getPixels();
	_currX = 0, _currY = 0;
	while (!stream->eos()) {
		// TODO: Handle odd-sized images
		if (_currX >= _width) {
			//assert (currX == width);
			_currX -= _width;
			_currY += 2;
			_pixels += _scanLen;
			if (_currY >= _height)
				break;
		}

		// Read the number and mode of the tiles
		byte op = stream->readByte();
		uint16 count;
		if ((op & 0xC0) != 0xC0) {
			count = op & 0x3F;
		} else {
			count = ((op & 0xF) << 8) + stream->readByte();
			op <<= 2;
		}
		op &= 0xC0;

		// Process the current serie
		for (int i = 0; i < count; i++) {
			switch (op) {
			case 0x00:
				// YCrCb
				processYCrCb();
				break;
			case 0x40:
				// Trans
				processTrans();
				break;
			case 0x80:
				// RGB
				processRGB();
				break;
			case 0xC0:
				error("Unsupported colour mode");
				break;
			}
			_pixels += 2;
		}
		_currX += 2 * count;
	}

	return surface;
}

void XMGDecoder::processYCrCb() {
	byte y0, y1, y2, y3;
	byte cr, cb;

	y0 = _stream->readByte();
	y1 = _stream->readByte();
	y2 = _stream->readByte();
	y3 = _stream->readByte();
	cr = _stream->readByte();
	cb = _stream->readByte();

	byte r, g, b;

	Graphics::YUV2RGB(y0, cb, cr, r, g, b);
	_pixels[0] = (255 << 24) + (b << 16) + (g << 8) + r;

	Graphics::YUV2RGB(y1, cb, cr, r, g, b);
	_pixels[1] = (255 << 24) + (b << 16) + (g << 8) + r;

	if (_currY + 1 < _height) {
		Graphics::YUV2RGB(y2, cb, cr, r, g, b);
		_pixels[_scanLen + 0] = (255 << 24) + (b << 16) + (g << 8) + r;

		Graphics::YUV2RGB(y3, cb, cr, r, g, b);
		_pixels[_scanLen + 1] = (255 << 24) + (b << 16) + (g << 8) + r;
	}
}

void XMGDecoder::processTrans() {
	_pixels[0] = _transColor;
	_pixels[1] = _transColor;

	if (_currY + 1 < _height) {
		_pixels[_scanLen + 0] = _transColor;
		_pixels[_scanLen + 1] = _transColor;
	}
}

void XMGDecoder::processRGB() {
	uint32 color;

	color = _stream->readUint16LE();
	color += _stream->readByte() << 16;
	if (color != _transColor)
		color += 255 << 24;
	_pixels[0] = color;

	color = _stream->readUint16LE();
	color += _stream->readByte() << 16;
	if (color != _transColor)
		color += 255 << 24;
	_pixels[1] = color;

	color = _stream->readUint16LE();
	color += _stream->readByte() << 16;

	if (_currY + 1 < _height) {
		if (color != _transColor)
			color += 255 << 24;
		_pixels[_scanLen + 0] = color;
	}

	color = _stream->readUint16LE();
	color += _stream->readByte() << 16;

	if (_currY + 1 < _height) {
		if (color != _transColor)
			color += 255 << 24;
		_pixels[_scanLen + 1] = color;
	}
}

} // End of namespace Formats
} // End of namespace Stark
