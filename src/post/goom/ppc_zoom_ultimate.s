.data
.text
.section regular,__DATA
.globl _ppc_zoom	;// name of the function to call by C program

; notes :
; this routine dynamically computes and applies a zoom filter
; do not use r0, r1, r2 and r13
; registers are not saved so the call to this function must be the last thing done in the calling C function

; parameters :
; r3  <=> unsigned int * frompixmap
; r4  <=> unsigned int * topixmap
; r5  <=> unsigned int sizeX (in pixels)
; r6  <=> unsigned int sizeY (in pixels)
; r7  <=> unsigned int * brutS
; r8  <=> unsigned int * brutD
; r9  <=> unsigned int buffratio

; globals after init
; r3  <=> frompixmap - 1 byte needed for preincremental fetch (replaces r3)
; r4  <=> topixmap - 1 byte needed for preincremental fetch (replaces r4)
; r5 <=> ax = x max in 16th of pixels (replaces old r5)
; r6 <=> ay = y max in 16th of pixels (replaces old r6)
; r15 <=> row size in bytes
; r16 <=> 0xFF00FF (mask for parallel 32 bits pixs computing)
; r30 <=> brutS - 1 byte needed for preincremental fetch (replaces r7)
; r31 <=> brutD - 1 byte needed for preincremental fetch (replaces r8)



_ppc_zoom:

; init
li      r16,0xFF
mullw   r17,r5,r6   ; number of pixels to compute
subi    r30,r8,4
mulli	r15,r5,4
slwi    r16,r16,16
subi    r5,r5,1
subi    r6,r6,1
mtspr	ctr,r17     ; number of pixels to compute
subi    r31,r7,4
subi    r4,r4,4
ori     r16,r16,0xFF
mulli   r5,r5,16
mulli   r6,r6,16


boucle:

; computes dynamically the position to fetch
lwzu	r8,4(r30)    ; px2
lwzu	r17,4(r31)   ; px
lwzu	r10,4(r30)   ; py2
lwzu	r29,4(r31)   ; py
sub     r8,r8,r17
sub     r10,r10,r29
mullw   r8,r8,r9
mullw   r10,r10,r9
srawi   r8,r8,16
srawi   r10,r10,16
add     r17,r17,r8
add     r29,r29,r10


; if px>ax or py>ay or px<0 or py <0 goto outofrange
cmpw 	cr1,r17,r5
bgt-	cr1,outofrange
cmpwi 	cr1,r17,0
blt-	cr1,outofrange
cmpw 	cr1,r29,r6
bgt-	cr1,outofrange
cmpwi 	cr1,r29,0
blt-	cr1,outofrange


; computes the attenuation coeffs
andi.   r8,r17,0x0F   ;coefh
andi.   r10,r29,0x0F  ;coefv
subfic  r18,r8,15     ;diffcoefh
subfic  r7,r10,15     ;diffcoefv
mullw   r21,r18,r7    ; coeff
mullw   r22,r8,r7     ; coeff << 8
mullw   r23,r18,r10   ; coeff << 16
mullw   r24,r8,r10    ; coeff << 24


; calcul de l adresse du point d origine
srawi   r17,r17,4    ; pertedec
srawi   r29,r29,4    ; pertedec
srwi    r7,r15,2
mullw   r29, r29,r7
add     r17,r17,r29
slwi    r17,r17,2
add	r17,r17,r3


; computes final pixel color
lwz	r25,0(r17)		; chargement de col1 ->r25
and	r8, r25,r16
lwz	r26,4(r17)		; chargement de col2 ->r26
mullw	r8, r8, r21
andi.	r18, r25,0xFF00
add	r17,r17,r15
and	r10, r26,r16
mullw	r18, r18, r21
lwz	r27,0(r17)		; chargement de col3 ->r27
mullw	r10, r10, r22
andi.	r29, r26, 0xFF00
lwz	r28,4(r17)		; chargement de col4 ->r28
add	r8, r8,r10
mullw	r29, r29, r22
and	r10, r27,r16
add	r18, r18, r29
mullw	r10, r10, r23
andi.	r29, r27, 0xFF00
add	r8, r8,r10
mullw	r29, r29, r23
and	r10, r28,r16
add	r18, r18, r29
mullw	r10, r10, r24
andi.	r29, r28, 0xFF00
add	r8, r8,r10
mullw	r29, r29, r24

srawi	r7,r8,8
add	r18, r18, r29
and	r7, r7,r16

srawi	r18, r18, 8
andi.   r18,r18,0xFF00
or	r7, r18, r7

b       end ;goto end


; if out of range
outofrange:
li      r7,0

end:
stwu	r7, 4(r4)


bdnz+	boucle


blr
