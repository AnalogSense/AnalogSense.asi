.DATA
	rage_input_helper_keyboard_array	dq	0 ; float(*)[256]
	set_value_from_keyboard_ret			dq	0
	set_value_from_mkb_axis_ret			dq	0

.CODE
	rage_input_helper_init PROC
		mov rage_input_helper_keyboard_array, rcx
		mov set_value_from_keyboard_ret, rdx
		mov set_value_from_mkb_axis_ret, r8
		ret
	rage_input_helper_init ENDP

	; detours, reading `edi` and assigning to `xmm6`

	set_value_from_keyboard_detour PROC
		mov r10, qword ptr [rage_input_helper_keyboard_array]
		movsxd r11, edi
		movss xmm6, dword ptr [r10 + 4*r11]
		cmp edi, 9
		mov r10, set_value_from_keyboard_ret
		jmp r10
	set_value_from_keyboard_detour ENDP

	set_value_from_mkb_axis_detour PROC
		mov r10, qword ptr [rage_input_helper_keyboard_array]

		mov r11d, edi
		shr r11d, 8
		movzx r11d, r11b
		movss xmm6, dword ptr [r10 + 4*r11]

		movzx r11, dil
		subss xmm6, dword ptr [r10 + 4*r11]

		mov r10, set_value_from_mkb_axis_ret
		jmp r10
	set_value_from_mkb_axis_detour ENDP

END
