
kernel.elf:     file format elf64-x86-64


Disassembly of section .text:

0000000000000000 <_start>:
   0:	48 89 d4             	mov    %rdx,%rsp
   3:	e9 00 00 00 00       	jmp    8 <kernel_main>

0000000000000008 <kernel_main>:
   8:	55                   	push   %rbp
   9:	48 89 e5             	mov    %rsp,%rbp
   c:	48 83 ec 30          	sub    $0x30,%rsp
  10:	48 89 7d e8          	mov    %rdi,-0x18(%rbp)
  14:	48 89 75 e0          	mov    %rsi,-0x20(%rbp)
  18:	48 89 55 d8          	mov    %rdx,-0x28(%rbp)
  1c:	e8 76 02 00 00       	call   297 <get_dateAndTime>
  21:	88 45 f9             	mov    %al,-0x7(%rbp)
  24:	0f b6 d4             	movzbl %ah,%edx
  27:	88 55 fa             	mov    %dl,-0x6(%rbp)
  2a:	48 89 c2             	mov    %rax,%rdx
  2d:	48 c1 ea 10          	shr    $0x10,%rdx
  31:	80 e6 ff             	and    $0xff,%dh
  34:	88 55 fb             	mov    %dl,-0x5(%rbp)
  37:	48 89 c2             	mov    %rax,%rdx
  3a:	48 c1 ea 18          	shr    $0x18,%rdx
  3e:	80 e6 ff             	and    $0xff,%dh
  41:	88 55 fc             	mov    %dl,-0x4(%rbp)
  44:	48 89 c2             	mov    %rax,%rdx
  47:	48 c1 ea 20          	shr    $0x20,%rdx
  4b:	80 e6 ff             	and    $0xff,%dh
  4e:	88 55 fd             	mov    %dl,-0x3(%rbp)
  51:	48 c1 e8 28          	shr    $0x28,%rax
  55:	80 e4 ff             	and    $0xff,%ah
  58:	88 45 fe             	mov    %al,-0x2(%rbp)
  5b:	48 8b 45 e0          	mov    -0x20(%rbp),%rax
  5f:	8b 38                	mov    (%rax),%edi
  61:	48 8b 45 e0          	mov    -0x20(%rbp),%rax
  65:	48 83 c0 0c          	add    $0xc,%rax
  69:	8b 08                	mov    (%rax),%ecx
  6b:	48 8b 45 e0          	mov    -0x20(%rbp),%rax
  6f:	48 83 c0 08          	add    $0x8,%rax
  73:	8b 10                	mov    (%rax),%edx
  75:	48 8b 45 e0          	mov    -0x20(%rbp),%rax
  79:	48 83 c0 04          	add    $0x4,%rax
  7d:	8b 30                	mov    (%rax),%esi
  7f:	48 8b 45 e8          	mov    -0x18(%rbp),%rax
  83:	48 83 c0 18          	add    $0x18,%rax
  87:	48 8b 00             	mov    (%rax),%rax
  8a:	41 89 f8             	mov    %edi,%r8d
  8d:	48 89 c7             	mov    %rax,%rdi
  90:	e8 aa 02 00 00       	call   33f <vga_init>
  95:	48 8b 45 d8          	mov    -0x28(%rbp),%rax
  99:	48 89 c6             	mov    %rax,%rsi
  9c:	48 8d 05 5d 0f 00 00 	lea    0xf5d(%rip),%rax        # 1000 <cursor_Setpos+0xf2>
  a3:	48 89 c7             	mov    %rax,%rdi
  a6:	b8 00 00 00 00       	mov    $0x0,%eax
  ab:	e8 45 09 00 00       	call   9f5 <printf>
  b0:	48 8b 45 e8          	mov    -0x18(%rbp),%rax
  b4:	48 83 c0 18          	add    $0x18,%rax
  b8:	48 8b 30             	mov    (%rax),%rsi
  bb:	48 8b 45 e8          	mov    -0x18(%rbp),%rax
  bf:	48 83 c0 10          	add    $0x10,%rax
  c3:	48 8b 08             	mov    (%rax),%rcx
  c6:	48 8b 45 e8          	mov    -0x18(%rbp),%rax
  ca:	48 83 c0 08          	add    $0x8,%rax
  ce:	48 8b 10             	mov    (%rax),%rdx
  d1:	48 8b 45 e8          	mov    -0x18(%rbp),%rax
  d5:	48 8b 00             	mov    (%rax),%rax
  d8:	49 89 f0             	mov    %rsi,%r8
  db:	48 89 c6             	mov    %rax,%rsi
  de:	48 8d 05 33 0f 00 00 	lea    0xf33(%rip),%rax        # 1018 <cursor_Setpos+0x10a>
  e5:	48 89 c7             	mov    %rax,%rdi
  e8:	b8 00 00 00 00       	mov    $0x0,%eax
  ed:	e8 03 09 00 00       	call   9f5 <printf>
  f2:	48 8d 05 74 0f 00 00 	lea    0xf74(%rip),%rax        # 106d <cursor_Setpos+0x15f>
  f9:	48 89 c7             	mov    %rax,%rdi
  fc:	b8 00 00 00 00       	mov    $0x0,%eax
 101:	e8 ef 08 00 00       	call   9f5 <printf>
 106:	0f b6 45 fe          	movzbl -0x2(%rbp),%eax
 10a:	0f b6 f0             	movzbl %al,%esi
 10d:	0f b6 45 fd          	movzbl -0x3(%rbp),%eax
 111:	44 0f b6 c0          	movzbl %al,%r8d
 115:	0f b6 45 fc          	movzbl -0x4(%rbp),%eax
 119:	0f b6 f8             	movzbl %al,%edi
 11c:	0f b6 45 f9          	movzbl -0x7(%rbp),%eax
 120:	0f b6 c8             	movzbl %al,%ecx
 123:	0f b6 45 fa          	movzbl -0x6(%rbp),%eax
 127:	0f b6 d0             	movzbl %al,%edx
 12a:	0f b6 45 fb          	movzbl -0x5(%rbp),%eax
 12e:	0f b6 c0             	movzbl %al,%eax
 131:	48 83 ec 08          	sub    $0x8,%rsp
 135:	56                   	push   %rsi
 136:	45 89 c1             	mov    %r8d,%r9d
 139:	41 89 f8             	mov    %edi,%r8d
 13c:	89 c6                	mov    %eax,%esi
 13e:	48 8d 05 2a 0f 00 00 	lea    0xf2a(%rip),%rax        # 106f <cursor_Setpos+0x161>
 145:	48 89 c7             	mov    %rax,%rdi
 148:	b8 00 00 00 00       	mov    $0x0,%eax
 14d:	e8 a3 08 00 00       	call   9f5 <printf>
 152:	48 83 c4 10          	add    $0x10,%rsp
 156:	48 89 45 f0          	mov    %rax,-0x10(%rbp)
 15a:	8b 55 f4             	mov    -0xc(%rbp),%edx
 15d:	8b 45 f0             	mov    -0x10(%rbp),%eax
 160:	89 d6                	mov    %edx,%esi
 162:	89 c7                	mov    %eax,%edi
 164:	e8 a5 0d 00 00       	call   f0e <cursor_Setpos>
 169:	0f b6 45 fe          	movzbl -0x2(%rbp),%eax
 16d:	88 45 ff             	mov    %al,-0x1(%rbp)
 170:	e8 22 01 00 00       	call   297 <get_dateAndTime>
 175:	88 45 f9             	mov    %al,-0x7(%rbp)
 178:	0f b6 d4             	movzbl %ah,%edx
 17b:	88 55 fa             	mov    %dl,-0x6(%rbp)
 17e:	48 89 c2             	mov    %rax,%rdx
 181:	48 c1 ea 10          	shr    $0x10,%rdx
 185:	80 e6 ff             	and    $0xff,%dh
 188:	88 55 fb             	mov    %dl,-0x5(%rbp)
 18b:	48 89 c2             	mov    %rax,%rdx
 18e:	48 c1 ea 18          	shr    $0x18,%rdx
 192:	80 e6 ff             	and    $0xff,%dh
 195:	88 55 fc             	mov    %dl,-0x4(%rbp)
 198:	48 89 c2             	mov    %rax,%rdx
 19b:	48 c1 ea 20          	shr    $0x20,%rdx
 19f:	80 e6 ff             	and    $0xff,%dh
 1a2:	88 55 fd             	mov    %dl,-0x3(%rbp)
 1a5:	48 c1 e8 28          	shr    $0x28,%rax
 1a9:	80 e4 ff             	and    $0xff,%ah
 1ac:	88 45 fe             	mov    %al,-0x2(%rbp)
 1af:	0f b6 45 fe          	movzbl -0x2(%rbp),%eax
 1b3:	38 45 ff             	cmp    %al,-0x1(%rbp)
 1b6:	74 b8                	je     170 <kernel_main+0x168>
 1b8:	0f b6 45 fe          	movzbl -0x2(%rbp),%eax
 1bc:	0f b6 f0             	movzbl %al,%esi
 1bf:	0f b6 45 fd          	movzbl -0x3(%rbp),%eax
 1c3:	44 0f b6 c0          	movzbl %al,%r8d
 1c7:	0f b6 45 fc          	movzbl -0x4(%rbp),%eax
 1cb:	0f b6 f8             	movzbl %al,%edi
 1ce:	0f b6 45 f9          	movzbl -0x7(%rbp),%eax
 1d2:	0f b6 c8             	movzbl %al,%ecx
 1d5:	0f b6 45 fa          	movzbl -0x6(%rbp),%eax
 1d9:	0f b6 d0             	movzbl %al,%edx
 1dc:	0f b6 45 fb          	movzbl -0x5(%rbp),%eax
 1e0:	0f b6 c0             	movzbl %al,%eax
 1e3:	48 83 ec 08          	sub    $0x8,%rsp
 1e7:	56                   	push   %rsi
 1e8:	45 89 c1             	mov    %r8d,%r9d
 1eb:	41 89 f8             	mov    %edi,%r8d
 1ee:	89 c6                	mov    %eax,%esi
 1f0:	48 8d 05 78 0e 00 00 	lea    0xe78(%rip),%rax        # 106f <cursor_Setpos+0x161>
 1f7:	48 89 c7             	mov    %rax,%rdi
 1fa:	b8 00 00 00 00       	mov    $0x0,%eax
 1ff:	e8 f1 07 00 00       	call   9f5 <printf>
 204:	48 83 c4 10          	add    $0x10,%rsp
 208:	48 89 45 f0          	mov    %rax,-0x10(%rbp)
 20c:	8b 55 f4             	mov    -0xc(%rbp),%edx
 20f:	8b 45 f0             	mov    -0x10(%rbp),%eax
 212:	89 d6                	mov    %edx,%esi
 214:	89 c7                	mov    %eax,%edi
 216:	e8 f3 0c 00 00       	call   f0e <cursor_Setpos>
 21b:	0f b6 45 fe          	movzbl -0x2(%rbp),%eax
 21f:	88 45 ff             	mov    %al,-0x1(%rbp)
 222:	e9 49 ff ff ff       	jmp    170 <kernel_main+0x168>
 227:	66 0f 1f 84 00 00 00 	nopw   0x0(%rax,%rax,1)
 22e:	00 00 

0000000000000230 <stack_get>:
 230:	48 89 e0             	mov    %rsp,%rax
 233:	48 83 c0 08          	add    $0x8,%rax
 237:	c3                   	ret
 238:	0f 1f 84 00 00 00 00 	nopl   0x0(%rax,%rax,1)
 23f:	00 

0000000000000240 <inb>:
 240:	66 89 fa             	mov    %di,%dx
 243:	ec                   	in     (%dx),%al
 244:	0f b6 c0             	movzbl %al,%eax
 247:	c3                   	ret

0000000000000248 <outb>:
 248:	66 89 fa             	mov    %di,%dx
 24b:	89 f0                	mov    %esi,%eax
 24d:	ee                   	out    %al,(%dx)
 24e:	c3                   	ret

000000000000024f <inw>:
 24f:	66 89 fa             	mov    %di,%dx
 252:	66 ed                	in     (%dx),%ax
 254:	0f b7 c0             	movzwl %ax,%eax
 257:	c3                   	ret

0000000000000258 <outw>:
 258:	66 89 fa             	mov    %di,%dx
 25b:	89 f0                	mov    %esi,%eax
 25d:	66 ef                	out    %ax,(%dx)
 25f:	c3                   	ret

0000000000000260 <io_wait>:
 260:	b0 00                	mov    $0x0,%al
 262:	66 ba 80 00          	mov    $0x80,%dx
 266:	ee                   	out    %al,(%dx)
 267:	c3                   	ret

0000000000000268 <rtc_read>:
 268:	55                   	push   %rbp
 269:	48 89 e5             	mov    %rsp,%rbp
 26c:	48 83 ec 10          	sub    $0x10,%rsp
 270:	89 f8                	mov    %edi,%eax
 272:	88 45 fc             	mov    %al,-0x4(%rbp)
 275:	0f b6 45 fc          	movzbl -0x4(%rbp),%eax
 279:	83 c8 80             	or     $0xffffff80,%eax
 27c:	0f b6 c0             	movzbl %al,%eax
 27f:	89 c6                	mov    %eax,%esi
 281:	bf 70 00 00 00       	mov    $0x70,%edi
 286:	e8 bd ff ff ff       	call   248 <outb>
 28b:	bf 71 00 00 00       	mov    $0x71,%edi
 290:	e8 ab ff ff ff       	call   240 <inb>
 295:	c9                   	leave
 296:	c3                   	ret

0000000000000297 <get_dateAndTime>:
 297:	55                   	push   %rbp
 298:	48 89 e5             	mov    %rsp,%rbp
 29b:	48 83 ec 10          	sub    $0x10,%rsp
 29f:	c7 45 f4 00 00 00 00 	movl   $0x0,-0xc(%rbp)
 2a6:	66 c7 45 f8 00 00    	movw   $0x0,-0x8(%rbp)
 2ac:	bf 09 00 00 00       	mov    $0x9,%edi
 2b1:	e8 b2 ff ff ff       	call   268 <rtc_read>
 2b6:	88 45 f4             	mov    %al,-0xc(%rbp)
 2b9:	bf 08 00 00 00       	mov    $0x8,%edi
 2be:	e8 a5 ff ff ff       	call   268 <rtc_read>
 2c3:	88 45 f5             	mov    %al,-0xb(%rbp)
 2c6:	bf 07 00 00 00       	mov    $0x7,%edi
 2cb:	e8 98 ff ff ff       	call   268 <rtc_read>
 2d0:	88 45 f6             	mov    %al,-0xa(%rbp)
 2d3:	bf 04 00 00 00       	mov    $0x4,%edi
 2d8:	e8 8b ff ff ff       	call   268 <rtc_read>
 2dd:	88 45 f7             	mov    %al,-0x9(%rbp)
 2e0:	bf 02 00 00 00       	mov    $0x2,%edi
 2e5:	e8 7e ff ff ff       	call   268 <rtc_read>
 2ea:	88 45 f8             	mov    %al,-0x8(%rbp)
 2ed:	bf 00 00 00 00       	mov    $0x0,%edi
 2f2:	e8 71 ff ff ff       	call   268 <rtc_read>
 2f7:	88 45 f9             	mov    %al,-0x7(%rbp)
 2fa:	8b 45 f4             	mov    -0xc(%rbp),%eax
 2fd:	89 45 fa             	mov    %eax,-0x6(%rbp)
 300:	0f b7 45 f8          	movzwl -0x8(%rbp),%eax
 304:	66 89 45 fe          	mov    %ax,-0x2(%rbp)
 308:	b8 00 00 00 00       	mov    $0x0,%eax
 30d:	8b 55 fa             	mov    -0x6(%rbp),%edx
 310:	89 d2                	mov    %edx,%edx
 312:	48 b9 00 00 00 00 ff 	movabs $0xffffffff00000000,%rcx
 319:	ff ff ff 
 31c:	48 21 c8             	and    %rcx,%rax
 31f:	48 09 d0             	or     %rdx,%rax
 322:	0f b7 55 fe          	movzwl -0x2(%rbp),%edx
 326:	0f b7 d2             	movzwl %dx,%edx
 329:	48 c1 e2 20          	shl    $0x20,%rdx
 32d:	48 b9 ff ff ff ff 00 	movabs $0xffff0000ffffffff,%rcx
 334:	00 ff ff 
 337:	48 21 c8             	and    %rcx,%rax
 33a:	48 09 d0             	or     %rdx,%rax
 33d:	c9                   	leave
 33e:	c3                   	ret

000000000000033f <vga_init>:
 33f:	55                   	push   %rbp
 340:	48 89 e5             	mov    %rsp,%rbp
 343:	48 83 ec 18          	sub    $0x18,%rsp
 347:	48 89 7d f8          	mov    %rdi,-0x8(%rbp)
 34b:	89 75 f4             	mov    %esi,-0xc(%rbp)
 34e:	89 55 f0             	mov    %edx,-0x10(%rbp)
 351:	89 4d ec             	mov    %ecx,-0x14(%rbp)
 354:	44 89 45 e8          	mov    %r8d,-0x18(%rbp)
 358:	48 8b 45 f8          	mov    -0x8(%rbp),%rax
 35c:	48 89 05 9d 3c 00 00 	mov    %rax,0x3c9d(%rip)        # 4000 <fb_addr>
 363:	8b 45 f4             	mov    -0xc(%rbp),%eax
 366:	0f af 45 f0          	imul   -0x10(%rbp),%eax
 36a:	89 05 a8 3c 00 00    	mov    %eax,0x3ca8(%rip)        # 4018 <size>
 370:	8b 45 f4             	mov    -0xc(%rbp),%eax
 373:	89 05 8f 3c 00 00    	mov    %eax,0x3c8f(%rip)        # 4008 <hor_res>
 379:	8b 45 ec             	mov    -0x14(%rbp),%eax
 37c:	89 05 8e 3c 00 00    	mov    %eax,0x3c8e(%rip)        # 4010 <p_scanln>
 382:	8b 45 f0             	mov    -0x10(%rbp),%eax
 385:	89 05 81 3c 00 00    	mov    %eax,0x3c81(%rip)        # 400c <ver_res>
 38b:	8b 45 e8             	mov    -0x18(%rbp),%eax
 38e:	89 05 80 3c 00 00    	mov    %eax,0x3c80(%rip)        # 4014 <p_format>
 394:	90                   	nop
 395:	c9                   	leave
 396:	c3                   	ret

0000000000000397 <draw_pixel>:
 397:	55                   	push   %rbp
 398:	48 89 e5             	mov    %rsp,%rbp
 39b:	48 83 ec 20          	sub    $0x20,%rsp
 39f:	89 7d ec             	mov    %edi,-0x14(%rbp)
 3a2:	89 75 e8             	mov    %esi,-0x18(%rbp)
 3a5:	89 55 e4             	mov    %edx,-0x1c(%rbp)
 3a8:	48 8b 05 51 3c 00 00 	mov    0x3c51(%rip),%rax        # 4000 <fb_addr>
 3af:	48 89 45 f8          	mov    %rax,-0x8(%rbp)
 3b3:	8b 05 57 3c 00 00    	mov    0x3c57(%rip),%eax        # 4010 <p_scanln>
 3b9:	0f af 45 e8          	imul   -0x18(%rbp),%eax
 3bd:	89 c2                	mov    %eax,%edx
 3bf:	8b 45 ec             	mov    -0x14(%rbp),%eax
 3c2:	01 d0                	add    %edx,%eax
 3c4:	89 45 f4             	mov    %eax,-0xc(%rbp)
 3c7:	8b 05 4b 3c 00 00    	mov    0x3c4b(%rip),%eax        # 4018 <size>
 3cd:	39 45 f4             	cmp    %eax,-0xc(%rbp)
 3d0:	7f 6e                	jg     440 <draw_pixel+0xa9>
 3d2:	8b 05 3c 3c 00 00    	mov    0x3c3c(%rip),%eax        # 4014 <p_format>
 3d8:	83 f8 01             	cmp    $0x1,%eax
 3db:	75 40                	jne    41d <draw_pixel+0x86>
 3dd:	8b 45 e4             	mov    -0x1c(%rbp),%eax
 3e0:	c1 e0 10             	shl    $0x10,%eax
 3e3:	25 00 00 ff 00       	and    $0xff0000,%eax
 3e8:	89 45 f0             	mov    %eax,-0x10(%rbp)
 3eb:	8b 45 e4             	mov    -0x1c(%rbp),%eax
 3ee:	25 00 ff 00 00       	and    $0xff00,%eax
 3f3:	01 45 f0             	add    %eax,-0x10(%rbp)
 3f6:	8b 45 e4             	mov    -0x1c(%rbp),%eax
 3f9:	c1 e8 10             	shr    $0x10,%eax
 3fc:	0f b6 c0             	movzbl %al,%eax
 3ff:	01 45 f0             	add    %eax,-0x10(%rbp)
 402:	8b 45 f4             	mov    -0xc(%rbp),%eax
 405:	48 98                	cltq
 407:	48 8d 14 85 00 00 00 	lea    0x0(,%rax,4),%rdx
 40e:	00 
 40f:	48 8b 45 f8          	mov    -0x8(%rbp),%rax
 413:	48 01 c2             	add    %rax,%rdx
 416:	8b 45 f0             	mov    -0x10(%rbp),%eax
 419:	89 02                	mov    %eax,(%rdx)
 41b:	eb 23                	jmp    440 <draw_pixel+0xa9>
 41d:	8b 05 f1 3b 00 00    	mov    0x3bf1(%rip),%eax        # 4014 <p_format>
 423:	85 c0                	test   %eax,%eax
 425:	75 19                	jne    440 <draw_pixel+0xa9>
 427:	8b 45 f4             	mov    -0xc(%rbp),%eax
 42a:	48 98                	cltq
 42c:	48 8d 14 85 00 00 00 	lea    0x0(,%rax,4),%rdx
 433:	00 
 434:	48 8b 45 f8          	mov    -0x8(%rbp),%rax
 438:	48 01 c2             	add    %rax,%rdx
 43b:	8b 45 e4             	mov    -0x1c(%rbp),%eax
 43e:	89 02                	mov    %eax,(%rdx)
 440:	90                   	nop
 441:	c9                   	leave
 442:	c3                   	ret

0000000000000443 <draw_16color>:
 443:	55                   	push   %rbp
 444:	48 89 e5             	mov    %rsp,%rbp
 447:	48 83 ec 30          	sub    $0x30,%rsp
 44b:	89 7d dc             	mov    %edi,-0x24(%rbp)
 44e:	89 75 d8             	mov    %esi,-0x28(%rbp)
 451:	89 55 d4             	mov    %edx,-0x2c(%rbp)
 454:	48 8b 05 a5 3b 00 00 	mov    0x3ba5(%rip),%rax        # 4000 <fb_addr>
 45b:	48 89 45 f8          	mov    %rax,-0x8(%rbp)
 45f:	8b 05 ab 3b 00 00    	mov    0x3bab(%rip),%eax        # 4010 <p_scanln>
 465:	0f af 45 d8          	imul   -0x28(%rbp),%eax
 469:	89 c2                	mov    %eax,%edx
 46b:	8b 45 dc             	mov    -0x24(%rbp),%eax
 46e:	01 d0                	add    %edx,%eax
 470:	89 45 f4             	mov    %eax,-0xc(%rbp)
 473:	8b 45 d4             	mov    -0x2c(%rbp),%eax
 476:	48 98                	cltq
 478:	48 8d 14 85 00 00 00 	lea    0x0(,%rax,4),%rdx
 47f:	00 
 480:	48 8d 05 79 1b 00 00 	lea    0x1b79(%rip),%rax        # 2000 <vga16_color>
 487:	8b 04 02             	mov    (%rdx,%rax,1),%eax
 48a:	89 45 f0             	mov    %eax,-0x10(%rbp)
 48d:	8b 05 85 3b 00 00    	mov    0x3b85(%rip),%eax        # 4018 <size>
 493:	39 45 f4             	cmp    %eax,-0xc(%rbp)
 496:	7f 6e                	jg     506 <draw_16color+0xc3>
 498:	8b 05 76 3b 00 00    	mov    0x3b76(%rip),%eax        # 4014 <p_format>
 49e:	83 f8 01             	cmp    $0x1,%eax
 4a1:	75 40                	jne    4e3 <draw_16color+0xa0>
 4a3:	8b 45 f0             	mov    -0x10(%rbp),%eax
 4a6:	c1 e0 10             	shl    $0x10,%eax
 4a9:	25 00 00 ff 00       	and    $0xff0000,%eax
 4ae:	89 45 ec             	mov    %eax,-0x14(%rbp)
 4b1:	8b 45 f0             	mov    -0x10(%rbp),%eax
 4b4:	25 00 ff 00 00       	and    $0xff00,%eax
 4b9:	01 45 ec             	add    %eax,-0x14(%rbp)
 4bc:	8b 45 f0             	mov    -0x10(%rbp),%eax
 4bf:	c1 e8 10             	shr    $0x10,%eax
 4c2:	0f b6 c0             	movzbl %al,%eax
 4c5:	01 45 ec             	add    %eax,-0x14(%rbp)
 4c8:	8b 45 f4             	mov    -0xc(%rbp),%eax
 4cb:	48 98                	cltq
 4cd:	48 8d 14 85 00 00 00 	lea    0x0(,%rax,4),%rdx
 4d4:	00 
 4d5:	48 8b 45 f8          	mov    -0x8(%rbp),%rax
 4d9:	48 01 c2             	add    %rax,%rdx
 4dc:	8b 45 ec             	mov    -0x14(%rbp),%eax
 4df:	89 02                	mov    %eax,(%rdx)
 4e1:	eb 23                	jmp    506 <draw_16color+0xc3>
 4e3:	8b 05 2b 3b 00 00    	mov    0x3b2b(%rip),%eax        # 4014 <p_format>
 4e9:	85 c0                	test   %eax,%eax
 4eb:	75 19                	jne    506 <draw_16color+0xc3>
 4ed:	8b 45 f4             	mov    -0xc(%rbp),%eax
 4f0:	48 98                	cltq
 4f2:	48 8d 14 85 00 00 00 	lea    0x0(,%rax,4),%rdx
 4f9:	00 
 4fa:	48 8b 45 f8          	mov    -0x8(%rbp),%rax
 4fe:	48 01 c2             	add    %rax,%rdx
 501:	8b 45 f0             	mov    -0x10(%rbp),%eax
 504:	89 02                	mov    %eax,(%rdx)
 506:	90                   	nop
 507:	c9                   	leave
 508:	c3                   	ret

0000000000000509 <Set_GlobalTextColor>:
 509:	55                   	push   %rbp
 50a:	48 89 e5             	mov    %rsp,%rbp
 50d:	48 83 ec 08          	sub    $0x8,%rsp
 511:	89 7d fc             	mov    %edi,-0x4(%rbp)
 514:	8b 45 fc             	mov    -0x4(%rbp),%eax
 517:	89 05 23 1b 00 00    	mov    %eax,0x1b23(%rip)        # 2040 <global_textcolor>
 51d:	90                   	nop
 51e:	c9                   	leave
 51f:	c3                   	ret

0000000000000520 <check_byte>:
 520:	55                   	push   %rbp
 521:	48 89 e5             	mov    %rsp,%rbp
 524:	48 83 ec 10          	sub    $0x10,%rsp
 528:	89 f1                	mov    %esi,%ecx
 52a:	89 d0                	mov    %edx,%eax
 52c:	89 fa                	mov    %edi,%edx
 52e:	88 55 fc             	mov    %dl,-0x4(%rbp)
 531:	89 ca                	mov    %ecx,%edx
 533:	88 55 f8             	mov    %dl,-0x8(%rbp)
 536:	88 45 f4             	mov    %al,-0xc(%rbp)
 539:	80 7d f4 01          	cmpb   $0x1,-0xc(%rbp)
 53d:	75 1a                	jne    559 <check_byte+0x39>
 53f:	0f b6 75 fc          	movzbl -0x4(%rbp),%esi
 543:	0f b6 45 f8          	movzbl -0x8(%rbp),%eax
 547:	ba 07 00 00 00       	mov    $0x7,%edx
 54c:	29 c2                	sub    %eax,%edx
 54e:	89 f0                	mov    %esi,%eax
 550:	89 d1                	mov    %edx,%ecx
 552:	d3 f8                	sar    %cl,%eax
 554:	88 45 fc             	mov    %al,-0x4(%rbp)
 557:	eb 11                	jmp    56a <check_byte+0x4a>
 559:	0f b6 55 fc          	movzbl -0x4(%rbp),%edx
 55d:	0f b6 45 f8          	movzbl -0x8(%rbp),%eax
 561:	89 c1                	mov    %eax,%ecx
 563:	d3 fa                	sar    %cl,%edx
 565:	89 d0                	mov    %edx,%eax
 567:	88 45 fc             	mov    %al,-0x4(%rbp)
 56a:	0f b6 45 fc          	movzbl -0x4(%rbp),%eax
 56e:	83 e0 01             	and    $0x1,%eax
 571:	c9                   	leave
 572:	c3                   	ret

0000000000000573 <draw_char>:
 573:	55                   	push   %rbp
 574:	48 89 e5             	mov    %rsp,%rbp
 577:	48 83 ec 20          	sub    $0x20,%rsp
 57b:	89 f8                	mov    %edi,%eax
 57d:	89 75 e8             	mov    %esi,-0x18(%rbp)
 580:	89 55 e4             	mov    %edx,-0x1c(%rbp)
 583:	88 45 ec             	mov    %al,-0x14(%rbp)
 586:	c1 65 e8 03          	shll   $0x3,-0x18(%rbp)
 58a:	c1 65 e4 04          	shll   $0x4,-0x1c(%rbp)
 58e:	0f be 45 ec          	movsbl -0x14(%rbp),%eax
 592:	c1 e0 04             	shl    $0x4,%eax
 595:	89 45 f4             	mov    %eax,-0xc(%rbp)
 598:	c7 45 fc 00 00 00 00 	movl   $0x0,-0x4(%rbp)
 59f:	e9 88 00 00 00       	jmp    62c <draw_char+0xb9>
 5a4:	8b 55 f4             	mov    -0xc(%rbp),%edx
 5a7:	8b 45 fc             	mov    -0x4(%rbp),%eax
 5aa:	01 d0                	add    %edx,%eax
 5ac:	48 98                	cltq
 5ae:	48 8d 15 ab 1a 00 00 	lea    0x1aab(%rip),%rdx        # 2060 <font_data>
 5b5:	0f b6 04 10          	movzbl (%rax,%rdx,1),%eax
 5b9:	88 45 f3             	mov    %al,-0xd(%rbp)
 5bc:	c6 45 fb 00          	movb   $0x0,-0x5(%rbp)
 5c0:	eb 58                	jmp    61a <draw_char+0xa7>
 5c2:	0f b6 4d fb          	movzbl -0x5(%rbp),%ecx
 5c6:	0f b6 45 f3          	movzbl -0xd(%rbp),%eax
 5ca:	ba 01 00 00 00       	mov    $0x1,%edx
 5cf:	89 ce                	mov    %ecx,%esi
 5d1:	89 c7                	mov    %eax,%edi
 5d3:	e8 48 ff ff ff       	call   520 <check_byte>
 5d8:	88 45 f2             	mov    %al,-0xe(%rbp)
 5db:	80 7d f2 01          	cmpb   $0x1,-0xe(%rbp)
 5df:	75 17                	jne    5f8 <draw_char+0x85>
 5e1:	8b 15 59 1a 00 00    	mov    0x1a59(%rip),%edx        # 2040 <global_textcolor>
 5e7:	8b 4d e4             	mov    -0x1c(%rbp),%ecx
 5ea:	8b 45 e8             	mov    -0x18(%rbp),%eax
 5ed:	89 ce                	mov    %ecx,%esi
 5ef:	89 c7                	mov    %eax,%edi
 5f1:	e8 4d fe ff ff       	call   443 <draw_16color>
 5f6:	eb 14                	jmp    60c <draw_char+0x99>
 5f8:	8b 4d e4             	mov    -0x1c(%rbp),%ecx
 5fb:	8b 45 e8             	mov    -0x18(%rbp),%eax
 5fe:	ba 00 00 00 00       	mov    $0x0,%edx
 603:	89 ce                	mov    %ecx,%esi
 605:	89 c7                	mov    %eax,%edi
 607:	e8 37 fe ff ff       	call   443 <draw_16color>
 60c:	83 45 e8 01          	addl   $0x1,-0x18(%rbp)
 610:	0f b6 45 fb          	movzbl -0x5(%rbp),%eax
 614:	83 c0 01             	add    $0x1,%eax
 617:	88 45 fb             	mov    %al,-0x5(%rbp)
 61a:	80 7d fb 07          	cmpb   $0x7,-0x5(%rbp)
 61e:	76 a2                	jbe    5c2 <draw_char+0x4f>
 620:	83 6d e8 08          	subl   $0x8,-0x18(%rbp)
 624:	83 45 e4 01          	addl   $0x1,-0x1c(%rbp)
 628:	83 45 fc 01          	addl   $0x1,-0x4(%rbp)
 62c:	83 7d fc 0f          	cmpl   $0xf,-0x4(%rbp)
 630:	0f 8e 6e ff ff ff    	jle    5a4 <draw_char+0x31>
 636:	90                   	nop
 637:	90                   	nop
 638:	c9                   	leave
 639:	c3                   	ret

000000000000063a <draw_cursor>:
 63a:	55                   	push   %rbp
 63b:	48 89 e5             	mov    %rsp,%rbp
 63e:	48 83 ec 18          	sub    $0x18,%rsp
 642:	89 7d ec             	mov    %edi,-0x14(%rbp)
 645:	8b 05 d5 39 00 00    	mov    0x39d5(%rip),%eax        # 4020 <cursor>
 64b:	c1 e0 03             	shl    $0x3,%eax
 64e:	89 45 f4             	mov    %eax,-0xc(%rbp)
 651:	8b 05 cd 39 00 00    	mov    0x39cd(%rip),%eax        # 4024 <cursor+0x4>
 657:	c1 e0 04             	shl    $0x4,%eax
 65a:	89 45 f0             	mov    %eax,-0x10(%rbp)
 65d:	c7 45 fc 00 00 00 00 	movl   $0x0,-0x4(%rbp)
 664:	eb 35                	jmp    69b <draw_cursor+0x61>
 666:	c7 45 f8 00 00 00 00 	movl   $0x0,-0x8(%rbp)
 66d:	eb 22                	jmp    691 <draw_cursor+0x57>
 66f:	8b 55 f0             	mov    -0x10(%rbp),%edx
 672:	8b 45 fc             	mov    -0x4(%rbp),%eax
 675:	8d 34 02             	lea    (%rdx,%rax,1),%esi
 678:	8b 55 f4             	mov    -0xc(%rbp),%edx
 67b:	8b 45 f8             	mov    -0x8(%rbp),%eax
 67e:	8d 0c 02             	lea    (%rdx,%rax,1),%ecx
 681:	8b 45 ec             	mov    -0x14(%rbp),%eax
 684:	89 c2                	mov    %eax,%edx
 686:	89 cf                	mov    %ecx,%edi
 688:	e8 b6 fd ff ff       	call   443 <draw_16color>
 68d:	83 45 f8 01          	addl   $0x1,-0x8(%rbp)
 691:	83 7d f8 07          	cmpl   $0x7,-0x8(%rbp)
 695:	7e d8                	jle    66f <draw_cursor+0x35>
 697:	83 45 fc 01          	addl   $0x1,-0x4(%rbp)
 69b:	83 7d fc 0f          	cmpl   $0xf,-0x4(%rbp)
 69f:	7e c5                	jle    666 <draw_cursor+0x2c>
 6a1:	90                   	nop
 6a2:	90                   	nop
 6a3:	c9                   	leave
 6a4:	c3                   	ret

00000000000006a5 <cursor_inc>:
 6a5:	55                   	push   %rbp
 6a6:	48 89 e5             	mov    %rsp,%rbp
 6a9:	8b 05 71 39 00 00    	mov    0x3971(%rip),%eax        # 4020 <cursor>
 6af:	83 c0 01             	add    $0x1,%eax
 6b2:	89 05 68 39 00 00    	mov    %eax,0x3968(%rip)        # 4020 <cursor>
 6b8:	8b 15 62 39 00 00    	mov    0x3962(%rip),%edx        # 4020 <cursor>
 6be:	8b 05 4c 39 00 00    	mov    0x394c(%rip),%eax        # 4010 <p_scanln>
 6c4:	c1 f8 03             	sar    $0x3,%eax
 6c7:	39 c2                	cmp    %eax,%edx
 6c9:	7c 19                	jl     6e4 <cursor_inc+0x3f>
 6cb:	c7 05 4b 39 00 00 00 	movl   $0x0,0x394b(%rip)        # 4020 <cursor>
 6d2:	00 00 00 
 6d5:	8b 05 49 39 00 00    	mov    0x3949(%rip),%eax        # 4024 <cursor+0x4>
 6db:	83 c0 01             	add    $0x1,%eax
 6de:	89 05 40 39 00 00    	mov    %eax,0x3940(%rip)        # 4024 <cursor+0x4>
 6e4:	90                   	nop
 6e5:	5d                   	pop    %rbp
 6e6:	c3                   	ret

00000000000006e7 <print_unsigned>:
 6e7:	55                   	push   %rbp
 6e8:	48 89 e5             	mov    %rsp,%rbp
 6eb:	48 83 ec 18          	sub    $0x18,%rsp
 6ef:	89 7d ec             	mov    %edi,-0x14(%rbp)
 6f2:	c7 45 fc 00 00 00 00 	movl   $0x0,-0x4(%rbp)
 6f9:	8b 4d ec             	mov    -0x14(%rbp),%ecx
 6fc:	89 ca                	mov    %ecx,%edx
 6fe:	b8 cd cc cc cc       	mov    $0xcccccccd,%eax
 703:	48 0f af c2          	imul   %rdx,%rax
 707:	48 c1 e8 20          	shr    $0x20,%rax
 70b:	89 c2                	mov    %eax,%edx
 70d:	c1 ea 03             	shr    $0x3,%edx
 710:	89 d0                	mov    %edx,%eax
 712:	c1 e0 02             	shl    $0x2,%eax
 715:	01 d0                	add    %edx,%eax
 717:	01 c0                	add    %eax,%eax
 719:	29 c1                	sub    %eax,%ecx
 71b:	89 ca                	mov    %ecx,%edx
 71d:	89 d0                	mov    %edx,%eax
 71f:	8d 48 30             	lea    0x30(%rax),%ecx
 722:	8b 45 fc             	mov    -0x4(%rbp),%eax
 725:	8d 50 01             	lea    0x1(%rax),%edx
 728:	89 55 fc             	mov    %edx,-0x4(%rbp)
 72b:	89 ca                	mov    %ecx,%edx
 72d:	48 98                	cltq
 72f:	88 54 05 f2          	mov    %dl,-0xe(%rbp,%rax,1)
 733:	8b 45 ec             	mov    -0x14(%rbp),%eax
 736:	89 c2                	mov    %eax,%edx
 738:	b8 cd cc cc cc       	mov    $0xcccccccd,%eax
 73d:	48 0f af c2          	imul   %rdx,%rax
 741:	48 c1 e8 20          	shr    $0x20,%rax
 745:	c1 e8 03             	shr    $0x3,%eax
 748:	89 45 ec             	mov    %eax,-0x14(%rbp)
 74b:	83 7d ec 00          	cmpl   $0x0,-0x14(%rbp)
 74f:	75 a8                	jne    6f9 <print_unsigned+0x12>
 751:	eb 2b                	jmp    77e <print_unsigned+0x97>
 753:	8b 15 cb 38 00 00    	mov    0x38cb(%rip),%edx        # 4024 <cursor+0x4>
 759:	8b 0d c1 38 00 00    	mov    0x38c1(%rip),%ecx        # 4020 <cursor>
 75f:	83 6d fc 01          	subl   $0x1,-0x4(%rbp)
 763:	8b 45 fc             	mov    -0x4(%rbp),%eax
 766:	48 98                	cltq
 768:	0f b6 44 05 f2       	movzbl -0xe(%rbp,%rax,1),%eax
 76d:	0f be c0             	movsbl %al,%eax
 770:	89 ce                	mov    %ecx,%esi
 772:	89 c7                	mov    %eax,%edi
 774:	e8 fa fd ff ff       	call   573 <draw_char>
 779:	e8 27 ff ff ff       	call   6a5 <cursor_inc>
 77e:	83 7d fc 00          	cmpl   $0x0,-0x4(%rbp)
 782:	7f cf                	jg     753 <print_unsigned+0x6c>
 784:	90                   	nop
 785:	90                   	nop
 786:	c9                   	leave
 787:	c3                   	ret

0000000000000788 <print_unsigned_long>:
 788:	55                   	push   %rbp
 789:	48 89 e5             	mov    %rsp,%rbp
 78c:	48 83 ec 30          	sub    $0x30,%rsp
 790:	48 89 7d d8          	mov    %rdi,-0x28(%rbp)
 794:	c7 45 fc 00 00 00 00 	movl   $0x0,-0x4(%rbp)
 79b:	48 8b 4d d8          	mov    -0x28(%rbp),%rcx
 79f:	48 ba cd cc cc cc cc 	movabs $0xcccccccccccccccd,%rdx
 7a6:	cc cc cc 
 7a9:	48 89 c8             	mov    %rcx,%rax
 7ac:	48 f7 e2             	mul    %rdx
 7af:	48 c1 ea 03          	shr    $0x3,%rdx
 7b3:	48 89 d0             	mov    %rdx,%rax
 7b6:	48 c1 e0 02          	shl    $0x2,%rax
 7ba:	48 01 d0             	add    %rdx,%rax
 7bd:	48 01 c0             	add    %rax,%rax
 7c0:	48 29 c1             	sub    %rax,%rcx
 7c3:	48 89 ca             	mov    %rcx,%rdx
 7c6:	89 d0                	mov    %edx,%eax
 7c8:	8d 48 30             	lea    0x30(%rax),%ecx
 7cb:	8b 45 fc             	mov    -0x4(%rbp),%eax
 7ce:	8d 50 01             	lea    0x1(%rax),%edx
 7d1:	89 55 fc             	mov    %edx,-0x4(%rbp)
 7d4:	89 ca                	mov    %ecx,%edx
 7d6:	48 98                	cltq
 7d8:	88 54 05 e0          	mov    %dl,-0x20(%rbp,%rax,1)
 7dc:	48 8b 45 d8          	mov    -0x28(%rbp),%rax
 7e0:	48 ba cd cc cc cc cc 	movabs $0xcccccccccccccccd,%rdx
 7e7:	cc cc cc 
 7ea:	48 f7 e2             	mul    %rdx
 7ed:	48 89 d0             	mov    %rdx,%rax
 7f0:	48 c1 e8 03          	shr    $0x3,%rax
 7f4:	48 89 45 d8          	mov    %rax,-0x28(%rbp)
 7f8:	48 83 7d d8 00       	cmpq   $0x0,-0x28(%rbp)
 7fd:	75 9c                	jne    79b <print_unsigned_long+0x13>
 7ff:	eb 26                	jmp    827 <print_unsigned_long+0x9f>
 801:	8b 15 1d 38 00 00    	mov    0x381d(%rip),%edx        # 4024 <cursor+0x4>
 807:	8b 0d 13 38 00 00    	mov    0x3813(%rip),%ecx        # 4020 <cursor>
 80d:	83 6d fc 01          	subl   $0x1,-0x4(%rbp)
 811:	8b 45 fc             	mov    -0x4(%rbp),%eax
 814:	48 98                	cltq
 816:	0f b6 44 05 e0       	movzbl -0x20(%rbp,%rax,1),%eax
 81b:	0f be c0             	movsbl %al,%eax
 81e:	89 ce                	mov    %ecx,%esi
 820:	89 c7                	mov    %eax,%edi
 822:	e8 4c fd ff ff       	call   573 <draw_char>
 827:	83 7d fc 00          	cmpl   $0x0,-0x4(%rbp)
 82b:	7f d4                	jg     801 <print_unsigned_long+0x79>
 82d:	e8 73 fe ff ff       	call   6a5 <cursor_inc>
 832:	90                   	nop
 833:	c9                   	leave
 834:	c3                   	ret

0000000000000835 <print_int>:
 835:	55                   	push   %rbp
 836:	48 89 e5             	mov    %rsp,%rbp
 839:	48 83 ec 18          	sub    $0x18,%rsp
 83d:	89 7d ec             	mov    %edi,-0x14(%rbp)
 840:	83 7d ec 00          	cmpl   $0x0,-0x14(%rbp)
 844:	79 31                	jns    877 <print_int+0x42>
 846:	8b 15 d8 37 00 00    	mov    0x37d8(%rip),%edx        # 4024 <cursor+0x4>
 84c:	8b 05 ce 37 00 00    	mov    0x37ce(%rip),%eax        # 4020 <cursor>
 852:	89 c6                	mov    %eax,%esi
 854:	bf 2d 00 00 00       	mov    $0x2d,%edi
 859:	e8 15 fd ff ff       	call   573 <draw_char>
 85e:	e8 42 fe ff ff       	call   6a5 <cursor_inc>
 863:	8b 45 ec             	mov    -0x14(%rbp),%eax
 866:	f7 d8                	neg    %eax
 868:	89 45 fc             	mov    %eax,-0x4(%rbp)
 86b:	8b 45 fc             	mov    -0x4(%rbp),%eax
 86e:	89 c7                	mov    %eax,%edi
 870:	e8 72 fe ff ff       	call   6e7 <print_unsigned>
 875:	eb 0a                	jmp    881 <print_int+0x4c>
 877:	8b 45 ec             	mov    -0x14(%rbp),%eax
 87a:	89 c7                	mov    %eax,%edi
 87c:	e8 66 fe ff ff       	call   6e7 <print_unsigned>
 881:	90                   	nop
 882:	c9                   	leave
 883:	c3                   	ret

0000000000000884 <print_long>:
 884:	55                   	push   %rbp
 885:	48 89 e5             	mov    %rsp,%rbp
 888:	48 83 ec 20          	sub    $0x20,%rsp
 88c:	48 89 7d e8          	mov    %rdi,-0x18(%rbp)
 890:	48 83 7d e8 00       	cmpq   $0x0,-0x18(%rbp)
 895:	79 36                	jns    8cd <print_long+0x49>
 897:	8b 15 87 37 00 00    	mov    0x3787(%rip),%edx        # 4024 <cursor+0x4>
 89d:	8b 05 7d 37 00 00    	mov    0x377d(%rip),%eax        # 4020 <cursor>
 8a3:	89 c6                	mov    %eax,%esi
 8a5:	bf 2d 00 00 00       	mov    $0x2d,%edi
 8aa:	e8 c4 fc ff ff       	call   573 <draw_char>
 8af:	e8 f1 fd ff ff       	call   6a5 <cursor_inc>
 8b4:	48 8b 45 e8          	mov    -0x18(%rbp),%rax
 8b8:	48 f7 d8             	neg    %rax
 8bb:	48 89 45 f8          	mov    %rax,-0x8(%rbp)
 8bf:	48 8b 45 f8          	mov    -0x8(%rbp),%rax
 8c3:	48 89 c7             	mov    %rax,%rdi
 8c6:	e8 bd fe ff ff       	call   788 <print_unsigned_long>
 8cb:	eb 0c                	jmp    8d9 <print_long+0x55>
 8cd:	48 8b 45 e8          	mov    -0x18(%rbp),%rax
 8d1:	48 89 c7             	mov    %rax,%rdi
 8d4:	e8 af fe ff ff       	call   788 <print_unsigned_long>
 8d9:	90                   	nop
 8da:	c9                   	leave
 8db:	c3                   	ret

00000000000008dc <print_byte_hex>:
 8dc:	55                   	push   %rbp
 8dd:	48 89 e5             	mov    %rsp,%rbp
 8e0:	48 83 ec 30          	sub    $0x30,%rsp
 8e4:	89 f8                	mov    %edi,%eax
 8e6:	88 45 dc             	mov    %al,-0x24(%rbp)
 8e9:	48 b8 30 31 32 33 34 	movabs $0x3736353433323130,%rax
 8f0:	35 36 37 
 8f3:	48 ba 38 39 41 42 43 	movabs $0x4645444342413938,%rdx
 8fa:	44 45 46 
 8fd:	48 89 45 e0          	mov    %rax,-0x20(%rbp)
 901:	48 89 55 e8          	mov    %rdx,-0x18(%rbp)
 905:	c6 45 f0 00          	movb   $0x0,-0x10(%rbp)
 909:	0f b6 45 dc          	movzbl -0x24(%rbp),%eax
 90d:	c0 e8 04             	shr    $0x4,%al
 910:	0f b6 c0             	movzbl %al,%eax
 913:	83 e0 0f             	and    $0xf,%eax
 916:	48 98                	cltq
 918:	0f b6 44 05 e0       	movzbl -0x20(%rbp,%rax,1),%eax
 91d:	88 45 ff             	mov    %al,-0x1(%rbp)
 920:	0f b6 45 dc          	movzbl -0x24(%rbp),%eax
 924:	83 e0 0f             	and    $0xf,%eax
 927:	48 98                	cltq
 929:	0f b6 44 05 e0       	movzbl -0x20(%rbp,%rax,1),%eax
 92e:	88 45 fe             	mov    %al,-0x2(%rbp)
 931:	8b 15 ed 36 00 00    	mov    0x36ed(%rip),%edx        # 4024 <cursor+0x4>
 937:	8b 0d e3 36 00 00    	mov    0x36e3(%rip),%ecx        # 4020 <cursor>
 93d:	0f be 45 ff          	movsbl -0x1(%rbp),%eax
 941:	89 ce                	mov    %ecx,%esi
 943:	89 c7                	mov    %eax,%edi
 945:	e8 29 fc ff ff       	call   573 <draw_char>
 94a:	e8 56 fd ff ff       	call   6a5 <cursor_inc>
 94f:	8b 15 cf 36 00 00    	mov    0x36cf(%rip),%edx        # 4024 <cursor+0x4>
 955:	8b 0d c5 36 00 00    	mov    0x36c5(%rip),%ecx        # 4020 <cursor>
 95b:	0f be 45 fe          	movsbl -0x2(%rbp),%eax
 95f:	89 ce                	mov    %ecx,%esi
 961:	89 c7                	mov    %eax,%edi
 963:	e8 0b fc ff ff       	call   573 <draw_char>
 968:	e8 38 fd ff ff       	call   6a5 <cursor_inc>
 96d:	90                   	nop
 96e:	c9                   	leave
 96f:	c3                   	ret

0000000000000970 <print_word_hex>:
 970:	55                   	push   %rbp
 971:	48 89 e5             	mov    %rsp,%rbp
 974:	48 83 ec 10          	sub    $0x10,%rsp
 978:	89 f8                	mov    %edi,%eax
 97a:	66 89 45 fc          	mov    %ax,-0x4(%rbp)
 97e:	0f b7 45 fc          	movzwl -0x4(%rbp),%eax
 982:	66 c1 e8 08          	shr    $0x8,%ax
 986:	0f b6 c0             	movzbl %al,%eax
 989:	89 c7                	mov    %eax,%edi
 98b:	e8 4c ff ff ff       	call   8dc <print_byte_hex>
 990:	0f b7 45 fc          	movzwl -0x4(%rbp),%eax
 994:	0f b6 c0             	movzbl %al,%eax
 997:	89 c7                	mov    %eax,%edi
 999:	e8 3e ff ff ff       	call   8dc <print_byte_hex>
 99e:	90                   	nop
 99f:	c9                   	leave
 9a0:	c3                   	ret

00000000000009a1 <print_dword_hex>:
 9a1:	55                   	push   %rbp
 9a2:	48 89 e5             	mov    %rsp,%rbp
 9a5:	48 83 ec 10          	sub    $0x10,%rsp
 9a9:	89 7d fc             	mov    %edi,-0x4(%rbp)
 9ac:	8b 45 fc             	mov    -0x4(%rbp),%eax
 9af:	c1 e8 10             	shr    $0x10,%eax
 9b2:	0f b7 c0             	movzwl %ax,%eax
 9b5:	89 c7                	mov    %eax,%edi
 9b7:	e8 b4 ff ff ff       	call   970 <print_word_hex>
 9bc:	8b 45 fc             	mov    -0x4(%rbp),%eax
 9bf:	0f b7 c0             	movzwl %ax,%eax
 9c2:	89 c7                	mov    %eax,%edi
 9c4:	e8 a7 ff ff ff       	call   970 <print_word_hex>
 9c9:	90                   	nop
 9ca:	c9                   	leave
 9cb:	c3                   	ret

00000000000009cc <print_qword_hex>:
 9cc:	55                   	push   %rbp
 9cd:	48 89 e5             	mov    %rsp,%rbp
 9d0:	48 83 ec 10          	sub    $0x10,%rsp
 9d4:	48 89 7d f8          	mov    %rdi,-0x8(%rbp)
 9d8:	48 8b 45 f8          	mov    -0x8(%rbp),%rax
 9dc:	48 c1 e8 20          	shr    $0x20,%rax
 9e0:	89 c7                	mov    %eax,%edi
 9e2:	e8 ba ff ff ff       	call   9a1 <print_dword_hex>
 9e7:	48 8b 45 f8          	mov    -0x8(%rbp),%rax
 9eb:	89 c7                	mov    %eax,%edi
 9ed:	e8 af ff ff ff       	call   9a1 <print_dword_hex>
 9f2:	90                   	nop
 9f3:	c9                   	leave
 9f4:	c3                   	ret

00000000000009f5 <printf>:
 9f5:	55                   	push   %rbp
 9f6:	48 89 e5             	mov    %rsp,%rbp
 9f9:	48 81 ec f0 00 00 00 	sub    $0xf0,%rsp
 a00:	48 89 bd 18 ff ff ff 	mov    %rdi,-0xe8(%rbp)
 a07:	48 89 b5 58 ff ff ff 	mov    %rsi,-0xa8(%rbp)
 a0e:	48 89 95 60 ff ff ff 	mov    %rdx,-0xa0(%rbp)
 a15:	48 89 8d 68 ff ff ff 	mov    %rcx,-0x98(%rbp)
 a1c:	4c 89 85 70 ff ff ff 	mov    %r8,-0x90(%rbp)
 a23:	4c 89 8d 78 ff ff ff 	mov    %r9,-0x88(%rbp)
 a2a:	84 c0                	test   %al,%al
 a2c:	74 20                	je     a4e <printf+0x59>
 a2e:	0f 29 45 80          	movaps %xmm0,-0x80(%rbp)
 a32:	0f 29 4d 90          	movaps %xmm1,-0x70(%rbp)
 a36:	0f 29 55 a0          	movaps %xmm2,-0x60(%rbp)
 a3a:	0f 29 5d b0          	movaps %xmm3,-0x50(%rbp)
 a3e:	0f 29 65 c0          	movaps %xmm4,-0x40(%rbp)
 a42:	0f 29 6d d0          	movaps %xmm5,-0x30(%rbp)
 a46:	0f 29 75 e0          	movaps %xmm6,-0x20(%rbp)
 a4a:	0f 29 7d f0          	movaps %xmm7,-0x10(%rbp)
 a4e:	c7 85 30 ff ff ff 08 	movl   $0x8,-0xd0(%rbp)
 a55:	00 00 00 
 a58:	c7 85 34 ff ff ff 30 	movl   $0x30,-0xcc(%rbp)
 a5f:	00 00 00 
 a62:	48 8d 45 10          	lea    0x10(%rbp),%rax
 a66:	48 89 85 38 ff ff ff 	mov    %rax,-0xc8(%rbp)
 a6d:	48 8d 85 50 ff ff ff 	lea    -0xb0(%rbp),%rax
 a74:	48 89 85 40 ff ff ff 	mov    %rax,-0xc0(%rbp)
 a7b:	8b 05 9f 35 00 00    	mov    0x359f(%rip),%eax        # 4020 <cursor>
 a81:	89 85 28 ff ff ff    	mov    %eax,-0xd8(%rbp)
 a87:	8b 05 97 35 00 00    	mov    0x3597(%rip),%eax        # 4024 <cursor+0x4>
 a8d:	89 85 2c ff ff ff    	mov    %eax,-0xd4(%rbp)
 a93:	48 8b 85 18 ff ff ff 	mov    -0xe8(%rbp),%rax
 a9a:	48 89 85 48 ff ff ff 	mov    %rax,-0xb8(%rbp)
 aa1:	e9 ed 03 00 00       	jmp    e93 <printf+0x49e>
 aa6:	48 8b 85 48 ff ff ff 	mov    -0xb8(%rbp),%rax
 aad:	0f b6 00             	movzbl (%rax),%eax
 ab0:	3c 0a                	cmp    $0xa,%al
 ab2:	75 1e                	jne    ad2 <printf+0xdd>
 ab4:	c7 05 62 35 00 00 00 	movl   $0x0,0x3562(%rip)        # 4020 <cursor>
 abb:	00 00 00 
 abe:	8b 05 60 35 00 00    	mov    0x3560(%rip),%eax        # 4024 <cursor+0x4>
 ac4:	83 c0 01             	add    $0x1,%eax
 ac7:	89 05 57 35 00 00    	mov    %eax,0x3557(%rip)        # 4024 <cursor+0x4>
 acd:	e9 b9 03 00 00       	jmp    e8b <printf+0x496>
 ad2:	48 8b 85 48 ff ff ff 	mov    -0xb8(%rbp),%rax
 ad9:	0f b6 00             	movzbl (%rax),%eax
 adc:	3c 25                	cmp    $0x25,%al
 ade:	0f 85 80 03 00 00    	jne    e64 <printf+0x46f>
 ae4:	48 83 85 48 ff ff ff 	addq   $0x1,-0xb8(%rbp)
 aeb:	01 
 aec:	48 8b 85 48 ff ff ff 	mov    -0xb8(%rbp),%rax
 af3:	0f b6 00             	movzbl (%rax),%eax
 af6:	0f be c0             	movsbl %al,%eax
 af9:	83 e8 62             	sub    $0x62,%eax
 afc:	83 f8 15             	cmp    $0x15,%eax
 aff:	0f 87 2e 03 00 00    	ja     e33 <printf+0x43e>
 b05:	89 c0                	mov    %eax,%eax
 b07:	48 8d 14 85 00 00 00 	lea    0x0(,%rax,4),%rdx
 b0e:	00 
 b0f:	48 8d 05 72 05 00 00 	lea    0x572(%rip),%rax        # 1088 <cursor_Setpos+0x17a>
 b16:	8b 04 02             	mov    (%rdx,%rax,1),%eax
 b19:	48 98                	cltq
 b1b:	48 8d 15 66 05 00 00 	lea    0x566(%rip),%rdx        # 1088 <cursor_Setpos+0x17a>
 b22:	48 01 d0             	add    %rdx,%rax
 b25:	ff e0                	jmp    *%rax
 b27:	8b 85 30 ff ff ff    	mov    -0xd0(%rbp),%eax
 b2d:	83 f8 2f             	cmp    $0x2f,%eax
 b30:	77 23                	ja     b55 <printf+0x160>
 b32:	48 8b 85 40 ff ff ff 	mov    -0xc0(%rbp),%rax
 b39:	8b 95 30 ff ff ff    	mov    -0xd0(%rbp),%edx
 b3f:	89 d2                	mov    %edx,%edx
 b41:	48 01 d0             	add    %rdx,%rax
 b44:	8b 95 30 ff ff ff    	mov    -0xd0(%rbp),%edx
 b4a:	83 c2 08             	add    $0x8,%edx
 b4d:	89 95 30 ff ff ff    	mov    %edx,-0xd0(%rbp)
 b53:	eb 12                	jmp    b67 <printf+0x172>
 b55:	48 8b 85 38 ff ff ff 	mov    -0xc8(%rbp),%rax
 b5c:	48 8d 50 08          	lea    0x8(%rax),%rdx
 b60:	48 89 95 38 ff ff ff 	mov    %rdx,-0xc8(%rbp)
 b67:	8b 00                	mov    (%rax),%eax
 b69:	89 c7                	mov    %eax,%edi
 b6b:	e8 c5 fc ff ff       	call   835 <print_int>
 b70:	e9 16 03 00 00       	jmp    e8b <printf+0x496>
 b75:	48 8b 85 48 ff ff ff 	mov    -0xb8(%rbp),%rax
 b7c:	48 83 c0 01          	add    $0x1,%rax
 b80:	0f b6 00             	movzbl (%rax),%eax
 b83:	3c 78                	cmp    $0x78,%al
 b85:	75 56                	jne    bdd <printf+0x1e8>
 b87:	8b 85 30 ff ff ff    	mov    -0xd0(%rbp),%eax
 b8d:	83 f8 2f             	cmp    $0x2f,%eax
 b90:	77 23                	ja     bb5 <printf+0x1c0>
 b92:	48 8b 85 40 ff ff ff 	mov    -0xc0(%rbp),%rax
 b99:	8b 95 30 ff ff ff    	mov    -0xd0(%rbp),%edx
 b9f:	89 d2                	mov    %edx,%edx
 ba1:	48 01 d0             	add    %rdx,%rax
 ba4:	8b 95 30 ff ff ff    	mov    -0xd0(%rbp),%edx
 baa:	83 c2 08             	add    $0x8,%edx
 bad:	89 95 30 ff ff ff    	mov    %edx,-0xd0(%rbp)
 bb3:	eb 12                	jmp    bc7 <printf+0x1d2>
 bb5:	48 8b 85 38 ff ff ff 	mov    -0xc8(%rbp),%rax
 bbc:	48 8d 50 08          	lea    0x8(%rax),%rdx
 bc0:	48 89 95 38 ff ff ff 	mov    %rdx,-0xc8(%rbp)
 bc7:	8b 00                	mov    (%rax),%eax
 bc9:	89 c7                	mov    %eax,%edi
 bcb:	e8 d1 fd ff ff       	call   9a1 <print_dword_hex>
 bd0:	48 83 85 48 ff ff ff 	addq   $0x1,-0xb8(%rbp)
 bd7:	01 
 bd8:	e9 ae 02 00 00       	jmp    e8b <printf+0x496>
 bdd:	8b 85 30 ff ff ff    	mov    -0xd0(%rbp),%eax
 be3:	83 f8 2f             	cmp    $0x2f,%eax
 be6:	77 23                	ja     c0b <printf+0x216>
 be8:	48 8b 85 40 ff ff ff 	mov    -0xc0(%rbp),%rax
 bef:	8b 95 30 ff ff ff    	mov    -0xd0(%rbp),%edx
 bf5:	89 d2                	mov    %edx,%edx
 bf7:	48 01 d0             	add    %rdx,%rax
 bfa:	8b 95 30 ff ff ff    	mov    -0xd0(%rbp),%edx
 c00:	83 c2 08             	add    $0x8,%edx
 c03:	89 95 30 ff ff ff    	mov    %edx,-0xd0(%rbp)
 c09:	eb 12                	jmp    c1d <printf+0x228>
 c0b:	48 8b 85 38 ff ff ff 	mov    -0xc8(%rbp),%rax
 c12:	48 8d 50 08          	lea    0x8(%rax),%rdx
 c16:	48 89 95 38 ff ff ff 	mov    %rdx,-0xc8(%rbp)
 c1d:	8b 00                	mov    (%rax),%eax
 c1f:	89 c7                	mov    %eax,%edi
 c21:	e8 c1 fa ff ff       	call   6e7 <print_unsigned>
 c26:	e9 60 02 00 00       	jmp    e8b <printf+0x496>
 c2b:	48 83 85 48 ff ff ff 	addq   $0x1,-0xb8(%rbp)
 c32:	01 
 c33:	48 8b 85 48 ff ff ff 	mov    -0xb8(%rbp),%rax
 c3a:	0f b6 00             	movzbl (%rax),%eax
 c3d:	3c 64                	cmp    $0x64,%al
 c3f:	75 50                	jne    c91 <printf+0x29c>
 c41:	8b 85 30 ff ff ff    	mov    -0xd0(%rbp),%eax
 c47:	83 f8 2f             	cmp    $0x2f,%eax
 c4a:	77 23                	ja     c6f <printf+0x27a>
 c4c:	48 8b 85 40 ff ff ff 	mov    -0xc0(%rbp),%rax
 c53:	8b 95 30 ff ff ff    	mov    -0xd0(%rbp),%edx
 c59:	89 d2                	mov    %edx,%edx
 c5b:	48 01 d0             	add    %rdx,%rax
 c5e:	8b 95 30 ff ff ff    	mov    -0xd0(%rbp),%edx
 c64:	83 c2 08             	add    $0x8,%edx
 c67:	89 95 30 ff ff ff    	mov    %edx,-0xd0(%rbp)
 c6d:	eb 12                	jmp    c81 <printf+0x28c>
 c6f:	48 8b 85 38 ff ff ff 	mov    -0xc8(%rbp),%rax
 c76:	48 8d 50 08          	lea    0x8(%rax),%rdx
 c7a:	48 89 95 38 ff ff ff 	mov    %rdx,-0xc8(%rbp)
 c81:	48 8b 00             	mov    (%rax),%rax
 c84:	48 89 c7             	mov    %rax,%rdi
 c87:	e8 f8 fb ff ff       	call   884 <print_long>
 c8c:	e9 fa 01 00 00       	jmp    e8b <printf+0x496>
 c91:	48 8b 85 48 ff ff ff 	mov    -0xb8(%rbp),%rax
 c98:	0f b6 00             	movzbl (%rax),%eax
 c9b:	3c 75                	cmp    $0x75,%al
 c9d:	75 50                	jne    cef <printf+0x2fa>
 c9f:	8b 85 30 ff ff ff    	mov    -0xd0(%rbp),%eax
 ca5:	83 f8 2f             	cmp    $0x2f,%eax
 ca8:	77 23                	ja     ccd <printf+0x2d8>
 caa:	48 8b 85 40 ff ff ff 	mov    -0xc0(%rbp),%rax
 cb1:	8b 95 30 ff ff ff    	mov    -0xd0(%rbp),%edx
 cb7:	89 d2                	mov    %edx,%edx
 cb9:	48 01 d0             	add    %rdx,%rax
 cbc:	8b 95 30 ff ff ff    	mov    -0xd0(%rbp),%edx
 cc2:	83 c2 08             	add    $0x8,%edx
 cc5:	89 95 30 ff ff ff    	mov    %edx,-0xd0(%rbp)
 ccb:	eb 12                	jmp    cdf <printf+0x2ea>
 ccd:	48 8b 85 38 ff ff ff 	mov    -0xc8(%rbp),%rax
 cd4:	48 8d 50 08          	lea    0x8(%rax),%rdx
 cd8:	48 89 95 38 ff ff ff 	mov    %rdx,-0xc8(%rbp)
 cdf:	48 8b 00             	mov    (%rax),%rax
 ce2:	48 89 c7             	mov    %rax,%rdi
 ce5:	e8 9e fa ff ff       	call   788 <print_unsigned_long>
 cea:	e9 9c 01 00 00       	jmp    e8b <printf+0x496>
 cef:	48 8b 85 48 ff ff ff 	mov    -0xb8(%rbp),%rax
 cf6:	0f b6 00             	movzbl (%rax),%eax
 cf9:	3c 78                	cmp    $0x78,%al
 cfb:	75 50                	jne    d4d <printf+0x358>
 cfd:	8b 85 30 ff ff ff    	mov    -0xd0(%rbp),%eax
 d03:	83 f8 2f             	cmp    $0x2f,%eax
 d06:	77 23                	ja     d2b <printf+0x336>
 d08:	48 8b 85 40 ff ff ff 	mov    -0xc0(%rbp),%rax
 d0f:	8b 95 30 ff ff ff    	mov    -0xd0(%rbp),%edx
 d15:	89 d2                	mov    %edx,%edx
 d17:	48 01 d0             	add    %rdx,%rax
 d1a:	8b 95 30 ff ff ff    	mov    -0xd0(%rbp),%edx
 d20:	83 c2 08             	add    $0x8,%edx
 d23:	89 95 30 ff ff ff    	mov    %edx,-0xd0(%rbp)
 d29:	eb 12                	jmp    d3d <printf+0x348>
 d2b:	48 8b 85 38 ff ff ff 	mov    -0xc8(%rbp),%rax
 d32:	48 8d 50 08          	lea    0x8(%rax),%rdx
 d36:	48 89 95 38 ff ff ff 	mov    %rdx,-0xc8(%rbp)
 d3d:	48 8b 00             	mov    (%rax),%rax
 d40:	48 89 c7             	mov    %rax,%rdi
 d43:	e8 84 fc ff ff       	call   9cc <print_qword_hex>
 d48:	e9 3e 01 00 00       	jmp    e8b <printf+0x496>
 d4d:	8b 15 d1 32 00 00    	mov    0x32d1(%rip),%edx        # 4024 <cursor+0x4>
 d53:	8b 05 c7 32 00 00    	mov    0x32c7(%rip),%eax        # 4020 <cursor>
 d59:	89 c6                	mov    %eax,%esi
 d5b:	bf 25 00 00 00       	mov    $0x25,%edi
 d60:	e8 0e f8 ff ff       	call   573 <draw_char>
 d65:	e8 3b f9 ff ff       	call   6a5 <cursor_inc>
 d6a:	8b 15 b4 32 00 00    	mov    0x32b4(%rip),%edx        # 4024 <cursor+0x4>
 d70:	8b 05 aa 32 00 00    	mov    0x32aa(%rip),%eax        # 4020 <cursor>
 d76:	89 c6                	mov    %eax,%esi
 d78:	bf 6c 00 00 00       	mov    $0x6c,%edi
 d7d:	e8 f1 f7 ff ff       	call   573 <draw_char>
 d82:	e8 1e f9 ff ff       	call   6a5 <cursor_inc>
 d87:	48 83 ad 48 ff ff ff 	subq   $0x1,-0xb8(%rbp)
 d8e:	01 
 d8f:	e9 f7 00 00 00       	jmp    e8b <printf+0x496>
 d94:	8b 85 30 ff ff ff    	mov    -0xd0(%rbp),%eax
 d9a:	83 f8 2f             	cmp    $0x2f,%eax
 d9d:	77 23                	ja     dc2 <printf+0x3cd>
 d9f:	48 8b 85 40 ff ff ff 	mov    -0xc0(%rbp),%rax
 da6:	8b 95 30 ff ff ff    	mov    -0xd0(%rbp),%edx
 dac:	89 d2                	mov    %edx,%edx
 dae:	48 01 d0             	add    %rdx,%rax
 db1:	8b 95 30 ff ff ff    	mov    -0xd0(%rbp),%edx
 db7:	83 c2 08             	add    $0x8,%edx
 dba:	89 95 30 ff ff ff    	mov    %edx,-0xd0(%rbp)
 dc0:	eb 12                	jmp    dd4 <printf+0x3df>
 dc2:	48 8b 85 38 ff ff ff 	mov    -0xc8(%rbp),%rax
 dc9:	48 8d 50 08          	lea    0x8(%rax),%rdx
 dcd:	48 89 95 38 ff ff ff 	mov    %rdx,-0xc8(%rbp)
 dd4:	8b 00                	mov    (%rax),%eax
 dd6:	0f b6 c0             	movzbl %al,%eax
 dd9:	89 c7                	mov    %eax,%edi
 ddb:	e8 fc fa ff ff       	call   8dc <print_byte_hex>
 de0:	e9 a6 00 00 00       	jmp    e8b <printf+0x496>
 de5:	8b 85 30 ff ff ff    	mov    -0xd0(%rbp),%eax
 deb:	83 f8 2f             	cmp    $0x2f,%eax
 dee:	77 23                	ja     e13 <printf+0x41e>
 df0:	48 8b 85 40 ff ff ff 	mov    -0xc0(%rbp),%rax
 df7:	8b 95 30 ff ff ff    	mov    -0xd0(%rbp),%edx
 dfd:	89 d2                	mov    %edx,%edx
 dff:	48 01 d0             	add    %rdx,%rax
 e02:	8b 95 30 ff ff ff    	mov    -0xd0(%rbp),%edx
 e08:	83 c2 08             	add    $0x8,%edx
 e0b:	89 95 30 ff ff ff    	mov    %edx,-0xd0(%rbp)
 e11:	eb 12                	jmp    e25 <printf+0x430>
 e13:	48 8b 85 38 ff ff ff 	mov    -0xc8(%rbp),%rax
 e1a:	48 8d 50 08          	lea    0x8(%rax),%rdx
 e1e:	48 89 95 38 ff ff ff 	mov    %rdx,-0xc8(%rbp)
 e25:	8b 00                	mov    (%rax),%eax
 e27:	0f b6 c0             	movzbl %al,%eax
 e2a:	89 c7                	mov    %eax,%edi
 e2c:	e8 ab fa ff ff       	call   8dc <print_byte_hex>
 e31:	eb 58                	jmp    e8b <printf+0x496>
 e33:	48 83 ad 48 ff ff ff 	subq   $0x1,-0xb8(%rbp)
 e3a:	01 
 e3b:	8b 15 e3 31 00 00    	mov    0x31e3(%rip),%edx        # 4024 <cursor+0x4>
 e41:	8b 0d d9 31 00 00    	mov    0x31d9(%rip),%ecx        # 4020 <cursor>
 e47:	48 8b 85 48 ff ff ff 	mov    -0xb8(%rbp),%rax
 e4e:	0f b6 00             	movzbl (%rax),%eax
 e51:	0f be c0             	movsbl %al,%eax
 e54:	89 ce                	mov    %ecx,%esi
 e56:	89 c7                	mov    %eax,%edi
 e58:	e8 16 f7 ff ff       	call   573 <draw_char>
 e5d:	e8 43 f8 ff ff       	call   6a5 <cursor_inc>
 e62:	eb 27                	jmp    e8b <printf+0x496>
 e64:	8b 15 ba 31 00 00    	mov    0x31ba(%rip),%edx        # 4024 <cursor+0x4>
 e6a:	8b 0d b0 31 00 00    	mov    0x31b0(%rip),%ecx        # 4020 <cursor>
 e70:	48 8b 85 48 ff ff ff 	mov    -0xb8(%rbp),%rax
 e77:	0f b6 00             	movzbl (%rax),%eax
 e7a:	0f be c0             	movsbl %al,%eax
 e7d:	89 ce                	mov    %ecx,%esi
 e7f:	89 c7                	mov    %eax,%edi
 e81:	e8 ed f6 ff ff       	call   573 <draw_char>
 e86:	e8 1a f8 ff ff       	call   6a5 <cursor_inc>
 e8b:	48 83 85 48 ff ff ff 	addq   $0x1,-0xb8(%rbp)
 e92:	01 
 e93:	48 8b 85 48 ff ff ff 	mov    -0xb8(%rbp),%rax
 e9a:	0f b6 00             	movzbl (%rax),%eax
 e9d:	84 c0                	test   %al,%al
 e9f:	0f 85 01 fc ff ff    	jne    aa6 <printf+0xb1>
 ea5:	48 8b 85 28 ff ff ff 	mov    -0xd8(%rbp),%rax
 eac:	c9                   	leave
 ead:	c3                   	ret

0000000000000eae <move_cursor>:
 eae:	55                   	push   %rbp
 eaf:	48 89 e5             	mov    %rsp,%rbp
 eb2:	48 83 ec 18          	sub    $0x18,%rsp
 eb6:	89 7d ec             	mov    %edi,-0x14(%rbp)
 eb9:	8b 05 51 31 00 00    	mov    0x3151(%rip),%eax        # 4010 <p_scanln>
 ebf:	c1 f8 03             	sar    $0x3,%eax
 ec2:	89 45 f8             	mov    %eax,-0x8(%rbp)
 ec5:	8b 05 59 31 00 00    	mov    0x3159(%rip),%eax        # 4024 <cursor+0x4>
 ecb:	0f af 45 f8          	imul   -0x8(%rbp),%eax
 ecf:	89 c2                	mov    %eax,%edx
 ed1:	8b 05 49 31 00 00    	mov    0x3149(%rip),%eax        # 4020 <cursor>
 ed7:	01 d0                	add    %edx,%eax
 ed9:	89 45 fc             	mov    %eax,-0x4(%rbp)
 edc:	8b 45 ec             	mov    -0x14(%rbp),%eax
 edf:	01 45 fc             	add    %eax,-0x4(%rbp)
 ee2:	83 7d fc 00          	cmpl   $0x0,-0x4(%rbp)
 ee6:	79 07                	jns    eef <move_cursor+0x41>
 ee8:	c7 45 fc 00 00 00 00 	movl   $0x0,-0x4(%rbp)
 eef:	8b 45 fc             	mov    -0x4(%rbp),%eax
 ef2:	99                   	cltd
 ef3:	f7 7d f8             	idivl  -0x8(%rbp)
 ef6:	89 05 28 31 00 00    	mov    %eax,0x3128(%rip)        # 4024 <cursor+0x4>
 efc:	8b 45 fc             	mov    -0x4(%rbp),%eax
 eff:	99                   	cltd
 f00:	f7 7d f8             	idivl  -0x8(%rbp)
 f03:	89 d0                	mov    %edx,%eax
 f05:	89 05 15 31 00 00    	mov    %eax,0x3115(%rip)        # 4020 <cursor>
 f0b:	90                   	nop
 f0c:	c9                   	leave
 f0d:	c3                   	ret

0000000000000f0e <cursor_Setpos>:
 f0e:	55                   	push   %rbp
 f0f:	48 89 e5             	mov    %rsp,%rbp
 f12:	48 83 ec 18          	sub    $0x18,%rsp
 f16:	89 7d ec             	mov    %edi,-0x14(%rbp)
 f19:	89 75 e8             	mov    %esi,-0x18(%rbp)
 f1c:	8b 05 ee 30 00 00    	mov    0x30ee(%rip),%eax        # 4010 <p_scanln>
 f22:	c1 f8 03             	sar    $0x3,%eax
 f25:	89 45 f8             	mov    %eax,-0x8(%rbp)
 f28:	8b 45 e8             	mov    -0x18(%rbp),%eax
 f2b:	0f af 45 f8          	imul   -0x8(%rbp),%eax
 f2f:	89 c2                	mov    %eax,%edx
 f31:	8b 45 ec             	mov    -0x14(%rbp),%eax
 f34:	01 d0                	add    %edx,%eax
 f36:	89 45 fc             	mov    %eax,-0x4(%rbp)
 f39:	83 7d fc 00          	cmpl   $0x0,-0x4(%rbp)
 f3d:	79 07                	jns    f46 <cursor_Setpos+0x38>
 f3f:	c7 45 fc 00 00 00 00 	movl   $0x0,-0x4(%rbp)
 f46:	8b 45 fc             	mov    -0x4(%rbp),%eax
 f49:	99                   	cltd
 f4a:	f7 7d f8             	idivl  -0x8(%rbp)
 f4d:	89 d0                	mov    %edx,%eax
 f4f:	89 05 cb 30 00 00    	mov    %eax,0x30cb(%rip)        # 4020 <cursor>
 f55:	8b 45 fc             	mov    -0x4(%rbp),%eax
 f58:	99                   	cltd
 f59:	f7 7d f8             	idivl  -0x8(%rbp)
 f5c:	89 05 c2 30 00 00    	mov    %eax,0x30c2(%rip)        # 4024 <cursor+0x4>
 f62:	90                   	nop
 f63:	c9                   	leave
 f64:	c3                   	ret
