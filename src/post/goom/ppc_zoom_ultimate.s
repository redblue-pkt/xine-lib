.text
;.section text,__TEXT
.globl _ppc_zoom	;// name of the function to call by C program

; notes :
; this routine dynamically computes and applies a zoom filter
; do not use r0, r1, r2 and r3
; registers are not saved so the call to this function must be the last thing done in the calling C function

; parameters :
; r3  <=> unsigned int * frompixmap
; r4  <=> unsigned int * topixmap
; r5  <=> unsigned int sizeX (in pixels)
; r6  <=> unsigned int sizeY (in pixels)
; r7  <=> unsigned int * brutS
; r8  <=> unsigned int * brutD
; r9  <=> unsigned int buffratio
; r10 <=> int [16][16] precalccoeffs

; globals after init
; r3  <=> frompixmap - 1 byte needed for preincremental fetch (replaces r3)
; r4  <=> topixmap - 1 byte needed for preincremental fetch (replaces r4)
; r5 <=> ax = x max in 16th of pixels (replaces old r5)
; r6 <=> ay = y max in 16th of pixels (replaces old r6)
; r15 <=> row size in bytes
; r16 <=> 0xFF00FF (mask for parallel 32 bits pixs computing)
; r30 <=> brutS - 1 byte needed for preincremental fetch (replaces r7)
; r31 <=> brutD - 1 byte needed for preincremental fetch (replaces r8)

; free reg
; r13
; r18


_ppc_zoom:		; symbole global sur lequel on va linker

; avant tout, on va sauver les registres
stw r13,-76(r1)
stw r14,-72(r1)
stw r15,-68(r1)
stw r16,-64(r1)
stw r17,-60(r1)
stw r18,-56(r1)
stw r19,-52(r1)
stw r20,-48(r1)
stw r21,-44(r1)
stw r22,-40(r1)
stw r23,-36(r1)
stw r24,-32(r1)
stw r25,-28(r1)
stw r26,-24(r1)
stw r27,-20(r1)
stw r28,-16(r1)
stw r29,-12(r1)
stw r30,-8(r1)
stw r31,-4(r1)


; init
dcbt	0,r8
li      r14,0		; valeur par defaut si out of range : 0 (Noir)
mr      r11,r10
lis     r16,0xFF
mullw   r17,r5,r6	; calcul du nombre de pixels a faire
dcbt	0,r7
subi    r30,r8,4
mulli	r15,r5,4
srawi   r19,r15,2
ori     r16,r16,0xFF
subi    r5,r5,1
subi    r6,r6,1
mtspr	ctr,r17		; on met le nombre de pixels a faire dans le compteur de la boucle
subi    r31,r7,4
subi    r4,r4,4
mulli   r5,r5,16
mulli   r6,r6,16
li	r13,32

;pre init for loop
lwzu	r17,4(r31)   ; px
lwzu	r8,4(r30)    ; px2
lwzu	r10,4(r30)   ; py2

1:

lwzu	r29,4(r31)   ; py

; computes dynamically the position to fetch
sub     r8,r8,r17
sub     r10,r10,r29
mullw   r8,r8,r9
mullw   r10,r10,r9
srawi   r8,r8,16
srawi   r10,r10,16
add     r17,r17,r8
add     r29,r29,r10


; if px>ax or py>ay goto outofrange
cmpl    cr1,0,r17,r5


; computes the attenuation coeffs and the original point address
andi.   r10,r29,0x0F  ;coefv
cmpl    cr2,0,r29,r6
andi.   r8,r17,0x0F   ;coefh
srawi   r29,r29,4     ; pos computing
bgt-	cr1,Loutofrange
srawi   r17,r17,4     ; pos computing
mulli   r10,r10,4
bgt-	cr2,Loutofrange
mullw   r29, r29,r19  ; pos computing

; NOTA : notation des couches : 00112233 (AARRVVBB)

mulli   r8,r8,4*16
add     r17,r17,r29    		; pos computing
add     r10,r10,r8
slwi    r17,r17,2      		; pos computing
add     r10,r10,r11
dcbt	0,r10
add	r17,r17,r3     		; pos computing
lwz     r10,0(r10)		; chargement des coefs
dcbt	0,r17
andi.   r21,r10,0xFF		; isolation du coef1
srwi    r10,r10,8		; isolation du coeff2 etape 1/2
lwz	r25,0(r17)		; chargement de col1 ->r25
andi.   r22,r10,0xFF		; isolation du coef2 etape 2/2
srwi    r10,r10,8		; isolation du coef3 etape 1/2
and	r8, r25,r16		; masquage de col1 couches 1 & 3 : 0x00XX00XX
lwz	r26,4(r17)		; chargement de col2 ->r26
andi.   r23,r10,0xFF		; isolation du coef3 etape 2/2
mullw	r8, r8, r21		; application du coef1 sur col1 couches 1 & 3
srwi    r10,r10,8		; isolation du coef4 etape 1/2
andi.	r25,r25,0xFF00		; masquage de col1 couche 2 : 0x0000XX00
add	r17,r17,r15		; ajout d'une ligne pour chargement futur de col3
dcbt	0,r17
andi.   r24,r10,0xFF		; isolation du coef4 etape 2/2



; computes final pixel color
and	r10,r26,r16		; masquage de col2 couches 1 & 3 : 0x00XX00XX
lwz	r27,0(r17)		; chargement de col3 ->r27
mullw	r25,r25,r21		; application du coef1 sur col1 couche 2
mullw	r10,r10,r22		; application du coef2 sur col2 couches 1 & 3
andi.	r29,r26,0xFF00		; masquage de col2 couche 2 : 0x0000XX00
lwz	r28,4(r17)		; chargement de col4 ->r28
add	r8 ,r8 ,r10		; ajout de col1 & col2 couches 1 & 3
mullw	r29,r29,r22		; application du coef2 sur col2 couche 2
and	r10,r27,r16		; masquage de col3 couches 1 & 3 : 0x00XX00XX
add	r25,r25,r29		; ajout de col1 & col2 couche 2
mullw	r10,r10,r23		; application du coef3 sur col3 couches 1 & 3
andi.	r29,r27,0xFF00		; masquage de col3 couche 2 : 0x0000XX00
add	r8 ,r8 ,r10		; ajout de col3 à (col1 + col2) couches 1 & 3
mullw	r29,r29,r23		; application du coef3 sur col3 couche 2
and	r10,r28,r16		; masquage de col4 couches 1 & 3 : 0x00XX00XX
add	r25,r25,r29		; ajout de col 3 à (col1 + col2) couche 2
mullw	r10,r10,r24		; application du coef4 sur col4 couches 1 & 3
andi.	r28,r28,0xFF00		; masquage de col4 couche 2 : 0x0000XX00
add	r8 ,r8 ,r10		; ajout de col4 à (col1 + col2 + col3) couches 1 & 3
lwzu	r17,4(r31)	; px
dcbt	0,r31
mullw	r28,r28,r24		; application du coef4 sur col4 couche 2

srawi	r7, r8, 8		; (somme des couches 1 & 3) >> 8
add	r25,r25,r28		; ajout de col 4 à (col1 + col2 + col3) couche 2
lwzu	r8,4(r30)    ; px2
and	r7, r7, r16		; masquage de la valeur résiduelle dans le résultat des couches 1 & 3

srawi	r25, r25, 8		; (somme des couches 2) >> 8
lwzu	r10,4(r30)   ; py2
andi.   r25,r25,0xFF00		; masquage de la valeur résiduelle dans le résultat des couches 2
or	r7, r25, r7		; association des couches (1 & 3) et 2
stwu	r7,4(r4)		; stockage du résultat final
bdnz+	1boucle			; itération suivante si besoin
b       Lend;goto end		; sinon sortie de boucle pour fin


; if out of range
Loutofrange:
stwu	r14,4(r4)
dcbtst	r13,r4		;touch for store
lwzu	r8,4(r30)    ; px2
lwzu	r10,4(r30)   ; py2
lwzu	r17,4(r31)   ; px
bdnz+	1boucle


Lend:		; Fin de la routine, on restore les registres utilisés entre 13 et 31

lwz r14,-76(r1)
lwz r14,-72(r1)
lwz r15,-68(r1)
lwz r16,-64(r1)
lwz r17,-60(r1)
lwz r18,-56(r1)
lwz r19,-52(r1)
lwz r20,-48(r1)
lwz r21,-44(r1)
lwz r22,-40(r1)
lwz r23,-36(r1)
lwz r24,-32(r1)
lwz r25,-28(r1)
lwz r26,-24(r1)
lwz r27,-20(r1)
lwz r28,-16(r1)
lwz r29,-12(r1)
lwz r30,-8(r1)
lwz r31,-4(r1)


blr		 ; et on retourne