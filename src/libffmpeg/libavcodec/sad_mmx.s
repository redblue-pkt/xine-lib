#  MMX/SSE optimized routines for SAD of 16*16 macroblocks
#       Copyright (C) Juan J. Sierralta P. <juanjo@atmlab.utfsm.cl>
#
#  dist1_* Original Copyright (C) 2000 Chris Atenasio <chris@crud.net>
#  Enhancements and rest Copyright (C) 2000 Andrew Stevens <as@comlab.ox.ac.uk>

#
#  This program is free software; you can redistribute it and/or
#  modify it under the terms of the GNU General Public License
#  as published by the Free Software Foundation; either version 2
#  of the License, or (at your option) any later version.
#
#  This program is distributed in the hope that it will be useful,
#  but WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#  GNU General Public License for more details.
#
#  You should have received a copy of the GNU General Public License
#  along with this program; if not, write to the Free Software
#  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
#

.global pix_abs16x16_mmx

# int  pix_abs16x16_mmx(unsigned char *pix1,unsigned char *pix2, int lx, int h);
# esi = p1 (init:               blk1)
# edi = p2 (init:               blk2)
# ecx = rowsleft (init:  h)
# edx = lx;

# mm0 = distance accumulators (4 words)
# mm1 = distance accumulators (4 words) 
# mm2 = temp 
# mm3 = temp
# mm4 = temp
# mm5 = temp 
# mm6 = 0
# mm7 = temp


.align 32
pix_abs16x16_mmx: 
        pushl                           %ebp                                    # save frame pointer
        movl                            %esp,%ebp

        pushl                           %ebx                                    # Saves registers (called saves convention in
        pushl                           %ecx                                    # x86 GCC it seems)
        pushl                           %edx                                    # 
        pushl                           %esi
        pushl                           %edi

        pxor                            %mm0,%mm0                               # zero acculumators
        pxor                            %mm1,%mm1
        pxor                            %mm6,%mm6
        movl                            8(%ebp),%esi            # get pix1
        movl                            12(%ebp),%edi           # get pix2
        movl                            16(%ebp),%edx           # get lx
        movl                            20(%ebp),%ecx           # get rowsleft
        jmp                             pix_abs16x16_mmx.nextrow
.align 32

pix_abs16x16_mmx.nextrow: 
        # First 8 bytes of the row

        movq                            (%edi),%mm4     # load first 8 bytes of pix2 row 
        movq                            (%esi),%mm5     # load first 8 bytes of pix1 row
        movq                            %mm4,%mm3               # mm4 := abs(mm4-mm5)
        movq                            8(%esi),%mm2    # load last 8 bytes of pix1 row 
        psubusb                 %mm5,%mm4
        movq                            8(%edi),%mm7    # load last 8 bytes of pix2 row                 
        psubusb                 %mm3,%mm5
        por                             %mm5,%mm4

        # Last 8 bytes of the row       

        movq                            %mm7,%mm3               # mm7 := abs(mm7-mm2)
        psubusb                 %mm2,%mm7
        psubusb                 %mm3,%mm2
        por                             %mm2,%mm7

        # Now mm4 and mm7 have 16 absdiffs to add

        # First 8 bytes of the row2


        addl                            %edx,%edi
        movq                            (%edi),%mm2     # load first 8 bytes of pix2 row 
        addl                            %edx,%esi
        movq                            (%esi),%mm5     # load first 8 bytes of pix1 row



        movq                            %mm2,%mm3               # mm2 := abs(mm2-mm5)
        psubusb                 %mm5,%mm2
        movq                            8(%esi),%mm6    # load last 8 bytes of pix1 row
        psubusb                 %mm3,%mm5
        por                             %mm5,%mm2

        # Last 8 bytes of the row2

        movq                            8(%edi),%mm5    # load last 8 bytes of pix2 row 


        movq                            %mm5,%mm3               # mm5 := abs(mm5-mm6)
        psubusb                 %mm6,%mm5
        psubusb                 %mm3,%mm6
        por                             %mm6,%mm5

        # Now mm2, mm4, mm5, mm7 have 32 absdiffs

        movq                            %mm7,%mm3

        pxor                            %mm6,%mm6               # Zero mm6

        punpcklbw               %mm6,%mm3               # Unpack to words and add
        punpckhbw               %mm6,%mm7
        paddusw                 %mm3,%mm7

        movq                            %mm5,%mm3

        punpcklbw               %mm6,%mm3               # Unpack to words and add
        punpckhbw               %mm6,%mm5
        paddusw                 %mm3,%mm5

        paddusw                 %mm7,%mm0               # Add to the acumulator (mm0)
        paddusw                 %mm5,%mm1               # Add to the acumulator (mm1)

        movq                            %mm4,%mm3

        punpcklbw               %mm6,%mm3               # Unpack to words and add
        punpckhbw               %mm6,%mm4
        movq                            %mm2,%mm5
        paddusw                 %mm3,%mm4



        punpcklbw               %mm6,%mm5               # Unpack to words and add
        punpckhbw               %mm6,%mm2
        paddusw                 %mm5,%mm2

        # Loop termination

        addl                            %edx,%esi               # update pointers to next row
        paddusw                 %mm4,%mm0               # Add to the acumulator (mm0)
        addl                            %edx,%edi
        subl                            $2,%ecx
        paddusw                 %mm2,%mm1               # Add to the acumulator (mm1)
        testl                           %ecx,%ecx               # check rowsleft
        jnz                             pix_abs16x16_mmx.nextrow

        paddusw                 %mm1,%mm0
        movq                            %mm0,%mm2               # Copy mm0 to mm2
        psrlq                           $32,%mm2
        paddusw                 %mm2,%mm0               # Add 
        movq                            %mm0,%mm3
        psrlq                           $16,%mm3
        paddusw                 %mm3,%mm0
        movd                            %mm0,%eax               # Store return value
        andl                            $0xffff,%eax

        popl %edi
        popl %esi
        popl %edx
        popl %ecx
        popl %ebx

        popl %ebp                                               # restore stack pointer

        #emms                                                           ; clear mmx registers
        ret                                                             # return

.global pix_abs16x16_sse

# int  pix_abs16x16_mmx(unsigned char *pix1,unsigned char *pix2, int lx, int h);
# esi = p1 (init:               blk1)
# edi = p2 (init:               blk2)
# ecx = rowsleft (init:  h)
# edx = lx;

# mm0 = distance accumulators (4 words)
# mm1 = distance accumulators (4 words) 
# mm2 = temp 
# mm3 = temp
# mm4 = temp
# mm5 = temp 
# mm6 = temp
# mm7 = temp


.align 32
pix_abs16x16_sse: 
        pushl                           %ebp                                    # save frame pointer
        movl                            %esp,%ebp

        pushl                           %ebx                                    # Saves registers (called saves convention in
        pushl                           %ecx                                    # x86 GCC it seems)
        pushl                           %edx                                    # 
        pushl                           %esi
        pushl                           %edi

        pxor                            %mm0,%mm0                               # zero acculumators
        pxor                            %mm1,%mm1
        movl                            8(%ebp),%esi            # get pix1
        movl                            12(%ebp),%edi           # get pix2
        movl                            16(%ebp),%edx           # get lx
        movl                            20(%ebp),%ecx           # get rowsleft
        jmp                             pix_abs16x16_sse.next4row
.align 32

pix_abs16x16_sse.next4row: 
        # First row

        movq                            (%edi),%mm4             # load first 8 bytes of pix2 row 
        movq                            8(%edi),%mm5    # load last 8 bytes of pix2 row
        psadbw                  (%esi),%mm4             # SAD of first 8 bytes
        psadbw                  8(%esi),%mm5    # SAD of last 8 bytes
        paddw                           %mm4,%mm0                       # Add to acumulators
        paddw                           %mm5,%mm1

        # Second row    

        addl                            %edx,%edi #
        addl                            %edx,%esi #

        movq                            (%edi),%mm6             # load first 8 bytes of pix2 row 
        movq                            8(%edi),%mm7    # load last 8 bytes of pix2 row
        psadbw                  (%esi),%mm6             # SAD of first 8 bytes
        psadbw                  8(%esi),%mm7    # SAD of last 8 bytes
        paddw                           %mm6,%mm0                       # Add to acumulators
        paddw                           %mm7,%mm1

        # Third row

        addl                            %edx,%edi #
        addl                            %edx,%esi #

        movq                            (%edi),%mm4             # load first 8 bytes of pix2 row 
        movq                            8(%edi),%mm5    # load last 8 bytes of pix2 row
        psadbw                  (%esi),%mm4             # SAD of first 8 bytes
        psadbw                  8(%esi),%mm5    # SAD of last 8 bytes
        paddw                           %mm4,%mm0                       # Add to acumulators
        paddw                           %mm5,%mm1

        # Fourth row    

        addl                            %edx,%edi #
        addl                            %edx,%esi #

        movq                            (%edi),%mm6             # load first 8 bytes of pix2 row 
        movq                            8(%edi),%mm7    # load last 8 bytes of pix2 row
        psadbw                  (%esi),%mm6             # SAD of first 8 bytes
        psadbw                  8(%esi),%mm7    # SAD of last 8 bytes
        paddw                           %mm6,%mm0                       # Add to acumulators
        paddw                           %mm7,%mm1

        # Loop termination

        addl                            %edx,%esi               # update pointers to next row
        addl                            %edx,%edi
        subl                            $4,%ecx
        testl                           %ecx,%ecx               # check rowsleft
        jnz                             pix_abs16x16_sse.next4row

        paddd                           %mm1,%mm0               # Sum acumulators
        movd                            %mm0,%eax               # Store return value

        popl %edi
        popl %esi
        popl %edx
        popl %ecx
        popl %ebx

        popl %ebp                                               # restore stack pointer

        #emms                                                           ; clear mmx registers
        ret                                                             # return

.global pix_abs16x16_x2_mmx

# int  pix_abs16x16_x2_mmx(unsigned char *pix1,unsigned char *pix2, int lx, int h);
# esi = p1 (init:               blk1)
# edi = p2 (init:               blk2)
# ecx = rowsleft (init:  h)
# edx = lx;

# mm0 = distance accumulators (4 words)
# mm1 = distance accumulators (4 words) 
# mm2 = temp 
# mm3 = temp
# mm4 = temp
# mm5 = temp 
# mm6 = 0
# mm7 = temp


.align 32
pix_abs16x16_x2_mmx: 
        pushl                           %ebp                                    # save frame pointer
        movl                            %esp,%ebp

        pushl                           %ebx                                    # Saves registers (called saves convention in
        pushl                           %ecx                                    # x86 GCC it seems)
        pushl                           %edx                                    # 
        pushl                           %esi
        pushl                           %edi

        pxor                            %mm0,%mm0                               # zero acculumators
        pxor                            %mm1,%mm1
        pxor                            %mm6,%mm6
        movl                            8(%ebp),%esi            # get pix1
        movl                            12(%ebp),%edi           # get pix2
        movl                            16(%ebp),%edx           # get lx
        movl                            20(%ebp),%ecx           # get rowsleft
        jmp                             pix_abs16x16_x2_mmx.nextrow_x2
.align 32

pix_abs16x16_x2_mmx.nextrow_x2: 
        # First 8 bytes of the row

        movq                            (%edi),%mm4                     # load first 8 bytes of pix2 row 
        movq                            1(%edi),%mm5            # load bytes 1-8 of pix2 row

        movq                            %mm4,%mm2               # copy mm4 on mm2
        movq                            %mm5,%mm3               # copy mm5 on mm3
        punpcklbw               %mm6,%mm4               # first 4 bytes of [edi] on mm4
        punpcklbw               %mm6,%mm5               # first 4 bytes of [edi+1] on mm5
        paddusw                 %mm5,%mm4               # mm4 := first 4 bytes interpolated in words
        psrlw                           $1,%mm4

        punpckhbw               %mm6,%mm2               # last 4 bytes of [edi] on mm2
        punpckhbw               %mm6,%mm3               # last 4 bytes of [edi+1] on mm3
        paddusw                 %mm3,%mm2               # mm2 := last 4 bytes interpolated in words
        psrlw                           $1,%mm2

        packuswb                        %mm2,%mm4       # pack 8 bytes interpolated on mm4
        movq                            (%esi),%mm5     # load first 8 bytes of pix1 row

        movq                            %mm4,%mm3               # mm4 := abs(mm4-mm5)
        psubusb                 %mm5,%mm4
        psubusb                 %mm3,%mm5
        por                             %mm5,%mm4

        # Last 8 bytes of the row       

        movq    8(%edi),%mm7                    # load last 8 bytes of pix2 row 
        movq    9(%edi),%mm5                    # load bytes 10-17 of pix2 row

        movq                            %mm7,%mm2               # copy mm7 on mm2
        movq                            %mm5,%mm3               # copy mm5 on mm3
        punpcklbw               %mm6,%mm7               # first 4 bytes of [edi+8] on mm7
        punpcklbw               %mm6,%mm5               # first 4 bytes of [edi+9] on mm5
        paddusw                 %mm5,%mm7               # mm1 := first 4 bytes interpolated in words
        psrlw                           $1,%mm7

        punpckhbw               %mm6,%mm2               # last 4 bytes of [edi] on mm2
        punpckhbw               %mm6,%mm3               # last 4 bytes of [edi+1] on mm3
        paddusw                 %mm3,%mm2               # mm2 := last 4 bytes interpolated in words
        psrlw                           $1,%mm2

        packuswb                        %mm2,%mm7       # pack 8 bytes interpolated on mm1
        movq                            8(%esi),%mm5    # load last 8 bytes of pix1 row

        movq                            %mm7,%mm3               # mm7 := abs(mm1-mm5)
        psubusb                 %mm5,%mm7
        psubusb                 %mm3,%mm5
        por                             %mm5,%mm7

        # Now mm4 and mm7 have 16 absdiffs to add

        movq                            %mm4,%mm3               # Make copies of these bytes
        movq                            %mm7,%mm2

        punpcklbw               %mm6,%mm4               # Unpack to words and add
        punpcklbw               %mm6,%mm7
        paddusw                 %mm7,%mm4
        paddusw                 %mm4,%mm0               # Add to the acumulator (mm0)

        punpckhbw               %mm6,%mm3               # Unpack to words and add
        punpckhbw               %mm6,%mm2
        paddusw                 %mm2,%mm3
        paddusw                 %mm3,%mm1               # Add to the acumulator (mm1)

        # Loop termination

        addl                            %edx,%esi               # update pointers to next row
        addl                            %edx,%edi

        subl                            $1,%ecx
        testl                           %ecx,%ecx               # check rowsleft
        jnz                             pix_abs16x16_x2_mmx.nextrow_x2

        paddusw                 %mm1,%mm0

        movq                            %mm0,%mm1               # Copy mm0 to mm1
        psrlq                           $32,%mm1
        paddusw                 %mm1,%mm0               # Add 
        movq                            %mm0,%mm2
        psrlq                           $16,%mm2
        paddusw                 %mm2,%mm0
        movd                            %mm0,%eax               # Store return value
        andl                            $0xffff,%eax

        popl %edi
        popl %esi
        popl %edx
        popl %ecx
        popl %ebx

        popl %ebp                                               # restore stack pointer

        emms                                                            # clear mmx registers
        ret                                                             # return

.global pix_abs16x16_y2_mmx

# int  pix_abs16x16_y2_mmx(unsigned char *pix1,unsigned char *pix2, int lx, int h);
# esi = p1 (init:               blk1)
# edi = p2 (init:               blk2)
# ebx = p2 + lx
# ecx = rowsleft (init:  h)
# edx = lx;

# mm0 = distance accumulators (4 words)
# mm1 = distance accumulators (4 words) 
# mm2 = temp 
# mm3 = temp
# mm4 = temp
# mm5 = temp 
# mm6 = 0
# mm7 = temp


.align 32
pix_abs16x16_y2_mmx: 
        pushl                           %ebp                                    # save frame pointer
        movl                            %esp,%ebp

        pushl                           %ebx                                    # Saves registers (called saves convention in
        pushl                           %ecx                                    # x86 GCC it seems)
        pushl                           %edx                                    # 
        pushl                           %esi
        pushl                           %edi

        pxor                            %mm0,%mm0                               # zero acculumators
        pxor                            %mm1,%mm1
        pxor                            %mm6,%mm6
        movl                            8(%ebp),%esi            # get pix1
        movl                            12(%ebp),%edi           # get pix2
        movl                            16(%ebp),%edx           # get lx
        movl                            20(%ebp),%ecx           # get rowsleft
        movl                            %edi,%ebx
        addl                            %edx,%ebx
        jmp                             pix_abs16x16_y2_mmx.nextrow_y2
.align 32

pix_abs16x16_y2_mmx.nextrow_y2: 
        # First 8 bytes of the row

        movq                            (%edi),%mm4                     # load first 8 bytes of pix2 row 
        movq                            (%ebx),%mm5                     # load bytes 1-8 of pix2 row

        movq                            %mm4,%mm2               # copy mm4 on mm2
        movq                            %mm5,%mm3               # copy mm5 on mm3
        punpcklbw               %mm6,%mm4               # first 4 bytes of [edi] on mm4
        punpcklbw               %mm6,%mm5               # first 4 bytes of [ebx] on mm5
        paddusw                 %mm5,%mm4               # mm4 := first 4 bytes interpolated in words
        psrlw                           $1,%mm4

        punpckhbw               %mm6,%mm2               # last 4 bytes of [edi] on mm2
        punpckhbw               %mm6,%mm3               # last 4 bytes of [edi+1] on mm3
        paddusw                 %mm3,%mm2               # mm2 := last 4 bytes interpolated in words
        psrlw                           $1,%mm2

        packuswb                        %mm2,%mm4       # pack 8 bytes interpolated on mm4
        movq                            (%esi),%mm5     # load first 8 bytes of pix1 row

        movq                            %mm4,%mm3               # mm4 := abs(mm4-mm5)
        psubusb                 %mm5,%mm4
        psubusb                 %mm3,%mm5
        por                             %mm5,%mm4

        # Last 8 bytes of the row       

        movq    8(%edi),%mm7                    # load last 8 bytes of pix2 row 
        movq    8(%ebx),%mm5                    # load bytes 10-17 of pix2 row

        movq                            %mm7,%mm2               # copy mm7 on mm2
        movq                            %mm5,%mm3               # copy mm5 on mm3
        punpcklbw               %mm6,%mm7               # first 4 bytes of [edi+8] on mm7
        punpcklbw               %mm6,%mm5               # first 4 bytes of [ebx+8] on mm5
        paddusw                 %mm5,%mm7               # mm1 := first 4 bytes interpolated in words
        psrlw                           $1,%mm7

        punpckhbw               %mm6,%mm2               # last 4 bytes of [edi+8] on mm2
        punpckhbw               %mm6,%mm3               # last 4 bytes of [ebx+8] on mm3
        paddusw                 %mm3,%mm2               # mm2 := last 4 bytes interpolated in words
        psrlw                           $1,%mm2

        packuswb                        %mm2,%mm7       # pack 8 bytes interpolated on mm1
        movq                            8(%esi),%mm5    # load last 8 bytes of pix1 row

        movq                            %mm7,%mm3               # mm7 := abs(mm1-mm5)
        psubusb                 %mm5,%mm7
        psubusb                 %mm3,%mm5
        por                             %mm5,%mm7

        # Now mm4 and mm7 have 16 absdiffs to add

        movq                            %mm4,%mm3               # Make copies of these bytes
        movq                            %mm7,%mm2

        punpcklbw               %mm6,%mm4               # Unpack to words and add
        punpcklbw               %mm6,%mm7
        paddusw                 %mm7,%mm4
        paddusw                 %mm4,%mm0               # Add to the acumulator (mm0)

        punpckhbw               %mm6,%mm3               # Unpack to words and add
        punpckhbw               %mm6,%mm2
        paddusw                 %mm2,%mm3
        paddusw                 %mm3,%mm1               # Add to the acumulator (mm1)

        # Loop termination

        addl                            %edx,%esi               # update pointers to next row
        addl                            %edx,%edi
        addl                            %edx,%ebx
        subl                            $1,%ecx
        testl                           %ecx,%ecx               # check rowsleft
        jnz                             pix_abs16x16_y2_mmx.nextrow_y2

        paddusw                 %mm1,%mm0

        movq                            %mm0,%mm1               # Copy mm0 to mm1
        psrlq                           $32,%mm1
        paddusw                 %mm1,%mm0               # Add 
        movq                            %mm0,%mm2
        psrlq                           $16,%mm2
        paddusw                 %mm2,%mm0
        movd                            %mm0,%eax               # Store return value
        andl                            $0xffff,%eax

        popl %edi
        popl %esi
        popl %edx
        popl %ecx
        popl %ebx

        popl %ebp                                               # restore stack pointer

        emms                                                            # clear mmx registers
        ret                                                             # return

.global pix_abs16x16_xy2_mmx

# int pix_abs16x16_xy2_mmx(unsigned char *p1,unsigned char *p2,int lx,int h);

# esi = p1 (init:               blk1)
# edi = p2 (init:               blk2)
# ebx = p1+lx
# ecx = rowsleft (init:  h)
# edx = lx;

# mm0 = distance accumulators (4 words)
# mm1 = bytes p2
# mm2 = bytes p1
# mm3 = bytes p1+lx
# I'd love to find someplace to stash p1+1 and p1+lx+1's bytes
# but I don't think thats going to happen in iA32-land...
# mm4 = temp 4 bytes in words interpolating p1, p1+1
# mm5 = temp 4 bytes in words from p2
# mm6 = temp comparison bit mask p1,p2
# mm7 = temp comparison bit mask p2,p1


.align 32
pix_abs16x16_xy2_mmx: 
        pushl %ebp              # save stack pointer
        movl %esp,%ebp  # so that we can do this

        pushl %ebx              # Saves registers (called saves convention in
        pushl %ecx              # x86 GCC it seems)
        pushl %edx              # 
        pushl %esi
        pushl %edi

        pxor %mm0,%mm0                          # zero acculumators

        movl 12(%ebp),%esi                      # get p1
        movl 8(%ebp),%edi                       # get p2
        movl 16(%ebp),%edx                      # get lx
        movl 20(%ebp),%ecx                      # rowsleft := h
        movl %esi,%ebx
    addl %edx,%ebx
        jmp pix_abs16x16_xy2_mmx.nextrowmm11                    # snap to it
.align 32
pix_abs16x16_xy2_mmx.nextrowmm11: 

                ## 
                ## First 8 bytes of row
                ## 

                ## First 4 bytes of 8

        movq (%esi),%mm4            # mm4 := first 4 bytes p1
        pxor %mm7,%mm7
        movq %mm4,%mm2                          #  mm2 records all 8 bytes
        punpcklbw %mm7,%mm4           #  First 4 bytes p1 in Words...

        movq (%ebx),%mm6                    #  mm6 := first 4 bytes p1+lx
        movq %mm6,%mm3              #  mm3 records all 8 bytes
        punpcklbw %mm7,%mm6
        paddw %mm6,%mm4


        movq 1(%esi),%mm5                       # mm5 := first 4 bytes p1+1
        punpcklbw %mm7,%mm5           #  First 4 bytes p1 in Words...
        paddw %mm5,%mm4
        movq 1(%ebx),%mm6           #  mm6 := first 4 bytes p1+lx+1
        punpcklbw %mm7,%mm6
        paddw %mm6,%mm4

        psrlw $2,%mm4               # mm4 := First 4 bytes interpolated in words

        movq (%edi),%mm5                        # mm5:=first 4 bytes of p2 in words
        movq %mm5,%mm1
        punpcklbw %mm7,%mm5

        movq  %mm4,%mm7
        pcmpgtw %mm5,%mm7       # mm7 := [i : W0..3,mm4>mm5]

        movq  %mm4,%mm6         # mm6 := [i : W0..3, (mm4-mm5)*(mm4-mm5 > 0)]
        psubw %mm5,%mm6
        pand  %mm7,%mm6

        paddw %mm6,%mm0                         # Add to accumulator

        movq  %mm5,%mm6     # mm6 := [i : W0..3,mm5>mm4]
        pcmpgtw %mm4,%mm6
        psubw %mm4,%mm5         # mm5 := [i : B0..7, (mm5-mm4)*(mm5-mm4 > 0)]
        pand  %mm6,%mm5

        paddw %mm5,%mm0                         # Add to accumulator

                ## Second 4 bytes of 8

        movq %mm2,%mm4              # mm4 := Second 4 bytes p1 in words
        pxor  %mm7,%mm7
        punpckhbw %mm7,%mm4
        movq %mm3,%mm6                  # mm6 := Second 4 bytes p1+1 in words  
        punpckhbw %mm7,%mm6
        paddw %mm6,%mm4

        movq 1(%esi),%mm5                       # mm5 := first 4 bytes p1+1
        punpckhbw %mm7,%mm5         #  First 4 bytes p1 in Words...
        paddw %mm5,%mm4
        movq 1(%ebx),%mm6           #  mm6 := first 4 bytes p1+lx+1
        punpckhbw %mm7,%mm6
        paddw %mm6,%mm4

        psrlw $2,%mm4               # mm4 := First 4 bytes interpolated in words

        movq %mm1,%mm5                  # mm5:= second 4 bytes of p2 in words
        punpckhbw %mm7,%mm5

        movq  %mm4,%mm7
        pcmpgtw %mm5,%mm7       # mm7 := [i : W0..3,mm4>mm5]

        movq  %mm4,%mm6         # mm6 := [i : W0..3, (mm4-mm5)*(mm4-mm5 > 0)]
        psubw %mm5,%mm6
        pand  %mm7,%mm6

        paddw %mm6,%mm0                         # Add to accumulator

        movq  %mm5,%mm6     # mm6 := [i : W0..3,mm5>mm4]
        pcmpgtw %mm4,%mm6
        psubw %mm4,%mm5         # mm5 := [i : B0..7, (mm5-mm4)*(mm5-mm4 > 0)]
        pand  %mm6,%mm5

        paddw %mm5,%mm0                         # Add to accumulator


                ##
                ## Second 8 bytes of row
                ## 
                ## First 4 bytes of 8

        movq 8(%esi),%mm4             # mm4 := first 4 bytes p1+8
        pxor %mm7,%mm7
        movq %mm4,%mm2                          #  mm2 records all 8 bytes
        punpcklbw %mm7,%mm4           #  First 4 bytes p1 in Words...

        movq 8(%ebx),%mm6                           #  mm6 := first 4 bytes p1+lx+8
        movq %mm6,%mm3              #  mm3 records all 8 bytes
        punpcklbw %mm7,%mm6
        paddw %mm6,%mm4


        movq 9(%esi),%mm5                       # mm5 := first 4 bytes p1+9
        punpcklbw %mm7,%mm5           #  First 4 bytes p1 in Words...
        paddw %mm5,%mm4
        movq 9(%ebx),%mm6           #  mm6 := first 4 bytes p1+lx+9
        punpcklbw %mm7,%mm6
        paddw %mm6,%mm4

        psrlw $2,%mm4               # mm4 := First 4 bytes interpolated in words

        movq 8(%edi),%mm5                               # mm5:=first 4 bytes of p2+8 in words
        movq %mm5,%mm1
        punpcklbw %mm7,%mm5

        movq  %mm4,%mm7
        pcmpgtw %mm5,%mm7       # mm7 := [i : W0..3,mm4>mm5]

        movq  %mm4,%mm6         # mm6 := [i : W0..3, (mm4-mm5)*(mm4-mm5 > 0)]
        psubw %mm5,%mm6
        pand  %mm7,%mm6

        paddw %mm6,%mm0                         # Add to accumulator

        movq  %mm5,%mm6     # mm6 := [i : W0..3,mm5>mm4]
        pcmpgtw %mm4,%mm6
        psubw %mm4,%mm5         # mm5 := [i : B0..7, (mm5-mm4)*(mm5-mm4 > 0)]
        pand  %mm6,%mm5

        paddw %mm5,%mm0                         # Add to accumulator

                ## Second 4 bytes of 8

        movq %mm2,%mm4              # mm4 := Second 4 bytes p1 in words
        pxor  %mm7,%mm7
        punpckhbw %mm7,%mm4
        movq %mm3,%mm6                  # mm6 := Second 4 bytes p1+1 in words  
        punpckhbw %mm7,%mm6
        paddw %mm6,%mm4

        movq 9(%esi),%mm5                       # mm5 := first 4 bytes p1+1
        punpckhbw %mm7,%mm5         #  First 4 bytes p1 in Words...
        paddw %mm5,%mm4
        movq 9(%ebx),%mm6           #  mm6 := first 4 bytes p1+lx+1
        punpckhbw %mm7,%mm6
        paddw %mm6,%mm4

        psrlw $2,%mm4               # mm4 := First 4 bytes interpolated in words

        movq %mm1,%mm5                  # mm5:= second 4 bytes of p2 in words
        punpckhbw %mm7,%mm5

        movq  %mm4,%mm7
        pcmpgtw %mm5,%mm7       # mm7 := [i : W0..3,mm4>mm5]

        movq  %mm4,%mm6         # mm6 := [i : W0..3, (mm4-mm5)*(mm4-mm5 > 0)]
        psubw %mm5,%mm6
        pand  %mm7,%mm6

        paddw %mm6,%mm0                         # Add to accumulator

        movq  %mm5,%mm6     # mm6 := [i : W0..3,mm5>mm4]
        pcmpgtw %mm4,%mm6
        psubw %mm4,%mm5         # mm5 := [i : B0..7, (mm5-mm4)*(mm5-mm4 > 0)]
        pand  %mm6,%mm5

        paddw %mm5,%mm0                         # Add to accumulator


                ##
                ##      Loop termination condition... and stepping
                ##              

        addl %edx,%esi          # update pointer to next row
        addl %edx,%edi          # ditto
        addl %edx,%ebx

        subl $1,%ecx
        testl %ecx,%ecx         # check rowsleft
        jnz pix_abs16x16_xy2_mmx.nextrowmm11

                ## Sum the Accumulators
        movq  %mm0,%mm4
        psrlq $32,%mm4
        paddw %mm4,%mm0
        movq  %mm0,%mm6
        psrlq $16,%mm6
        paddw %mm6,%mm0
        movd %mm0,%eax          # store return value
        andl $0xffff,%eax

        popl %edi
        popl %esi
        popl %edx
        popl %ecx
        popl %ebx

        popl %ebp               # restore stack pointer

        emms                    # clear mmx registers
        ret                     # we now return you to your regular programming



