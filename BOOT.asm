; MyOS boot loader.
                .386
                .model  tiny
                .code
		org	7C00h
                jmp     word ptr boot
; Bootloader data
disk_id                 db      (0)
boot_msg		db	"MyOS boot loader. Version 0.01",0Dh,0Ah,0
reboot_msg		db	"Press any key...",0Dh,0Ah,0

																																	

; Output string DS:SI on the screen
write_str:
		push	AX
		push	SI
		mov	AH, 0Eh
wr_loop:
		lodsb
		test	AL, AL
		jz	end_wr
		int	10h
		jmp	wr_loop 																																				      
end_wr:
		pop	SI
		pop	AX
		ret

; Critical error
error:		pop	SI
		call	write_str
; Reboot
reboot:
		mov	SI, word ptr reboot_msg
		call	write_str
		xor	AH, AH
		int	16h


                jmp     large dword ptr 0FFFFh:[0]
                ;jmp      0FFFFh:0
; Bootloader entry point
boot:
		; Set segment register
		jmp	0:start_boot
start_boot:
		mov	AX, CS
		mov	DS, AX
		mov	ES, AX
		; Set stack
		mov	SS, AX
                mov     SP, word ptr disk_id
		; Enable interrupts
		sti
		; Remember the boot disk number 																																							   
                mov     byte ptr disk_id, DL
		; Output the welcome message
                mov     SI, word ptr boot_msg
		call	write_str
		; Ending
		jmp	reboot		      

; Empty space and signature
db      510 - ($ - disk_id) dup (0)
db	55h, 0AAh
                end     write_str

