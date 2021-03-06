/*
 * MIT License
 *
 * Copyright (c) 2017-2018 Ingenia-CAT S.L.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "frame.h"

#include "ingenialink/err.h"
#include "ingenialink/utils.h"

#include <string.h>

/*******************************************************************************
 * Internal
 ******************************************************************************/

void il_eth_mcb_frame__swap(uint8_t *frame, size_t sz)
{
	size_t i;

	for (i = 0; i < sz; i += 2) {
		uint8_t tmp;

		tmp = frame[i];
		frame[i] = frame[i + 1];
		frame[i + 1] = tmp;
	}
}

void il_eth_mcb_frame__get_address(uint16_t *frame) 
{
	// uint16_t addr;

	// memcpy(&addr, &frame->buf[FR_INDEX_H_FLD], sizeof(idx));
	// idx = __swap_index(idx);
	// sidx = frame->buf[FR_SINDEX_FLD];

	// return IL_EUSB_FRAME_ADDR(idx, sidx);
}
