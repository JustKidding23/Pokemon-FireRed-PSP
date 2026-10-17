	.include "asm/macros/bit_width.inc"
	.macro loadtiles address
	.byte 0
	ptrvalue \address
	.endm

	.macro loadfadedpal address
	.byte 1
	ptrvalue \address
	.endm

	.macro loadpal address
	.byte 2
	ptrvalue \address
	.endm

	.macro callnative address
	.byte 3
	ptrvalue \address
	.endm

	.macro end
	.byte 4
	.endm

	.macro loadgfx_callnative tiles_address, palette_address, function_address
	.byte 5
	ptrvalue \tiles_address
	ptrvalue \palette_address
	ptrvalue \function_address
	.endm

	.macro loadtiles_callnative tiles_address, function_address
	.byte 6
	ptrvalue \tiles_address
	ptrvalue \function_address
	.endm

	.macro loadfadedpal_callnative palette_address, function_address
	.byte 7
	ptrvalue \palette_address
	ptrvalue \function_address
	.endm
