
kernel.elf:     file format elf64-x86-64


Disassembly of section .text:

0000000000000000 <main>:
   0:	55                   	push   %rbp
   1:	48 89 e5             	mov    %rsp,%rbp
   4:	48 83 ec 20          	sub    $0x20,%rsp
   8:	48 89 7d e8          	mov    %rdi,-0x18(%rbp)
   c:	48 89 75 e0          	mov    %rsi,-0x20(%rbp)
  10:	48 8b 45 e0          	mov    -0x20(%rbp),%rax
  14:	8b 38                	mov    (%rax),%edi
  16:	48 8b 45 e0          	mov    -0x20(%rbp),%rax
  1a:	48 83 c0 0c          	add    $0xc,%rax
  1e:	8b 08                	mov    (%rax),%ecx
  20:	48 8b 45 e0          	mov    -0x20(%rbp),%rax
  24:	48 83 c0 08          	add    $0x8,%rax
  28:	8b 10                	mov    (%rax),%edx
  2a:	48 8b 45 e0          	mov    -0x20(%rbp),%rax
  2e:	48 83 c0 04          	add    $0x4,%rax
  32:	8b 30                	mov    (%rax),%esi
  34:	48 8b 45 e8          	mov    -0x18(%rbp),%rax
  38:	48 83 c0 18          	add    $0x18,%rax
  3c:	48 8b 00             	mov    (%rax),%rax
  3f:	41 89 f8             	mov    %edi,%r8d
  42:	48 89 c7             	mov    %rax,%rdi
  45:	e8 97 01 00 00       	call   1e1 <vga_init>
  4a:	48 8b 45 e8          	mov    -0x18(%rbp),%rax
  4e:	48 83 c0 18          	add    $0x18,%rax
  52:	48 8b 30             	mov    (%rax),%rsi
  55:	48 8b 45 e8          	mov    -0x18(%rbp),%rax
  59:	48 83 c0 10          	add    $0x10,%rax
  5d:	48 8b 08             	mov    (%rax),%rcx
  60:	48 8b 45 e8          	mov    -0x18(%rbp),%rax
  64:	48 83 c0 08          	add    $0x8,%rax
  68:	48 8b 10             	mov    (%rax),%rdx
  6b:	48 8b 45 e8          	mov    -0x18(%rbp),%rax
  6f:	48 8b 00             	mov    (%rax),%rax
  72:	49 89 f0             	mov    %rsi,%r8
  75:	48 89 c6             	mov    %rax,%rsi
  78:	48 8d 05 81 0f 00 00 	lea    0xf81(%rip),%rax        # 1000 <cursor_Setpos+0x250>
  7f:	48 89 c7             	mov    %rax,%rdi
  82:	b8 00 00 00 00       	mov    $0x0,%eax
  87:	e8 0b 08 00 00       	call   897 <printf>
  8c:	48 8d 05 c2 0f 00 00 	lea    0xfc2(%rip),%rax        # 1055 <cursor_Setpos+0x2a5>
  93:	48 89 c7             	mov    %rax,%rdi
  96:	b8 00 00 00 00       	mov    $0x0,%eax
  9b:	e8 f7 07 00 00       	call   897 <printf>
  a0:	e8 92 00 00 00       	call   137 <print_date>
  a5:	bf 00 00 00 00       	mov    $0x0,%edi
  aa:	e8 59 00 00 00       	call   108 <rtc_read>
  af:	88 45 ff             	mov    %al,-0x1(%rbp)
  b2:	bf 00 00 00 00       	mov    $0x0,%edi
  b7:	e8 4c 00 00 00       	call   108 <rtc_read>
  bc:	88 45 fe             	mov    %al,-0x2(%rbp)
  bf:	0f b6 45 fe          	movzbl -0x2(%rbp),%eax
  c3:	3a 45 ff             	cmp    -0x1(%rbp),%al
  c6:	74 ea                	je     b2 <main+0xb2>
  c8:	e8 6a 00 00 00       	call   137 <print_date>
  cd:	0f b6 45 fe          	movzbl -0x2(%rbp),%eax
  d1:	88 45 ff             	mov    %al,-0x1(%rbp)
  d4:	eb dc                	jmp    b2 <main+0xb2>
  d6:	66 2e 0f 1f 84 00 00 	cs nopw 0x0(%rax,%rax,1)
  dd:	00 00 00 

00000000000000e0 <inb>:
  e0:	66 89 fa             	mov    %di,%dx
  e3:	ec                   	in     (%dx),%al
  e4:	0f b6 c0             	movzbl %al,%eax
  e7:	c3                   	ret

00000000000000e8 <outb>:
  e8:	66 89 fa             	mov    %di,%dx
  eb:	89 f0                	mov    %esi,%eax
  ed:	ee                   	out    %al,(%dx)
  ee:	c3                   	ret

00000000000000ef <inw>:
  ef:	66 89 fa             	mov    %di,%dx
  f2:	66 ed                	in     (%dx),%ax
  f4:	0f b7 c0             	movzwl %ax,%eax
  f7:	c3                   	ret

00000000000000f8 <outw>:
  f8:	66 89 fa             	mov    %di,%dx
  fb:	89 f0                	mov    %esi,%eax
  fd:	66 ef                	out    %ax,(%dx)
  ff:	c3                   	ret

0000000000000100 <io_wait>:
 100:	b0 00                	mov    $0x0,%al
 102:	66 ba 80 00          	mov    $0x80,%dx
 106:	ee                   	out    %al,(%dx)
 107:	c3                   	ret

0000000000000108 <rtc_read>:
 108:	55                   	push   %rbp
 109:	48 89 e5             	mov    %rsp,%rbp
 10c:	48 83 ec 10          	sub    $0x10,%rsp
 110:	89 f8                	mov    %edi,%eax
 112:	88 45 fc             	mov    %al,-0x4(%rbp)
 115:	0f b6 45 fc          	movzbl -0x4(%rbp),%eax
 119:	83 c8 80             	or     $0xffffff80,%eax
 11c:	0f b6 c0             	movzbl %al,%eax
 11f:	89 c6                	mov    %eax,%esi
 121:	bf 70 00 00 00       	mov    $0x70,%edi
 126:	e8 bd ff ff ff       	call   e8 <outb>
 12b:	bf 71 00 00 00       	mov    $0x71,%edi
 130:	e8 ab ff ff ff       	call   e0 <inb>
 135:	c9                   	leave
 136:	c3                   	ret

0000000000000137 <print_date>:
 137:	55                   	push   %rbp
 138:	48 89 e5             	mov    %rsp,%rbp
 13b:	48 83 ec 10          	sub    $0x10,%rsp
 13f:	bf 09 00 00 00       	mov    $0x9,%edi
 144:	e8 bf ff ff ff       	call   108 <rtc_read>
 149:	88 45 ff             	mov    %al,-0x1(%rbp)
 14c:	bf 08 00 00 00       	mov    $0x8,%edi
 151:	e8 b2 ff ff ff       	call   108 <rtc_read>
 156:	88 45 fe             	mov    %al,-0x2(%rbp)
 159:	bf 07 00 00 00       	mov    $0x7,%edi
 15e:	e8 a5 ff ff ff       	call   108 <rtc_read>
 163:	88 45 fd             	mov    %al,-0x3(%rbp)
 166:	bf 04 00 00 00       	mov    $0x4,%edi
 16b:	e8 98 ff ff ff       	call   108 <rtc_read>
 170:	88 45 fc             	mov    %al,-0x4(%rbp)
 173:	bf 02 00 00 00       	mov    $0x2,%edi
 178:	e8 8b ff ff ff       	call   108 <rtc_read>
 17d:	88 45 fb             	mov    %al,-0x5(%rbp)
 180:	bf 00 00 00 00       	mov    $0x0,%edi
 185:	e8 7e ff ff ff       	call   108 <rtc_read>
 18a:	88 45 fa             	mov    %al,-0x6(%rbp)
 18d:	0f b6 75 fa          	movzbl -0x6(%rbp),%esi
 191:	44 0f b6 45 fb       	movzbl -0x5(%rbp),%r8d
 196:	0f b6 7d fc          	movzbl -0x4(%rbp),%edi
 19a:	0f b6 4d ff          	movzbl -0x1(%rbp),%ecx
 19e:	0f b6 55 fe          	movzbl -0x2(%rbp),%edx
 1a2:	0f b6 45 fd          	movzbl -0x3(%rbp),%eax
 1a6:	48 83 ec 08          	sub    $0x8,%rsp
 1aa:	56                   	push   %rsi
 1ab:	45 89 c1             	mov    %r8d,%r9d
 1ae:	41 89 f8             	mov    %edi,%r8d
 1b1:	89 c6                	mov    %eax,%esi
 1b3:	48 8d 05 9e 0e 00 00 	lea    0xe9e(%rip),%rax        # 1058 <cursor_Setpos+0x2a8>
 1ba:	48 89 c7             	mov    %rax,%rdi
 1bd:	b8 00 00 00 00       	mov    $0x0,%eax
 1c2:	e8 d0 06 00 00       	call   897 <printf>
 1c7:	48 83 c4 10          	add    $0x10,%rsp
 1cb:	48 89 45 f0          	mov    %rax,-0x10(%rbp)
 1cf:	8b 55 f4             	mov    -0xc(%rbp),%edx
 1d2:	8b 45 f0             	mov    -0x10(%rbp),%eax
 1d5:	89 d6                	mov    %edx,%esi
 1d7:	89 c7                	mov    %eax,%edi
 1d9:	e8 d2 0b 00 00       	call   db0 <cursor_Setpos>
 1de:	90                   	nop
 1df:	c9                   	leave
 1e0:	c3                   	ret

00000000000001e1 <vga_init>:
 1e1:	55                   	push   %rbp
 1e2:	48 89 e5             	mov    %rsp,%rbp
 1e5:	48 83 ec 18          	sub    $0x18,%rsp
 1e9:	48 89 7d f8          	mov    %rdi,-0x8(%rbp)
 1ed:	89 75 f4             	mov    %esi,-0xc(%rbp)
 1f0:	89 55 f0             	mov    %edx,-0x10(%rbp)
 1f3:	89 4d ec             	mov    %ecx,-0x14(%rbp)
 1f6:	44 89 45 e8          	mov    %r8d,-0x18(%rbp)
 1fa:	48 8b 45 f8          	mov    -0x8(%rbp),%rax
 1fe:	48 89 05 fb 3d 00 00 	mov    %rax,0x3dfb(%rip)        # 4000 <fb_addr>
 205:	8b 45 f4             	mov    -0xc(%rbp),%eax
 208:	0f af 45 f0          	imul   -0x10(%rbp),%eax
 20c:	89 05 06 3e 00 00    	mov    %eax,0x3e06(%rip)        # 4018 <size>
 212:	8b 45 f4             	mov    -0xc(%rbp),%eax
 215:	89 05 ed 3d 00 00    	mov    %eax,0x3ded(%rip)        # 4008 <hor_res>
 21b:	8b 45 ec             	mov    -0x14(%rbp),%eax
 21e:	89 05 ec 3d 00 00    	mov    %eax,0x3dec(%rip)        # 4010 <p_scanln>
 224:	8b 45 f0             	mov    -0x10(%rbp),%eax
 227:	89 05 df 3d 00 00    	mov    %eax,0x3ddf(%rip)        # 400c <ver_res>
 22d:	8b 45 e8             	mov    -0x18(%rbp),%eax
 230:	89 05 de 3d 00 00    	mov    %eax,0x3dde(%rip)        # 4014 <p_format>
 236:	90                   	nop
 237:	c9                   	leave
 238:	c3                   	ret

0000000000000239 <draw_pixel>:
 239:	55                   	push   %rbp
 23a:	48 89 e5             	mov    %rsp,%rbp
 23d:	48 83 ec 20          	sub    $0x20,%rsp
 241:	89 7d ec             	mov    %edi,-0x14(%rbp)
 244:	89 75 e8             	mov    %esi,-0x18(%rbp)
 247:	89 55 e4             	mov    %edx,-0x1c(%rbp)
 24a:	48 8b 05 af 3d 00 00 	mov    0x3daf(%rip),%rax        # 4000 <fb_addr>
 251:	48 89 45 f8          	mov    %rax,-0x8(%rbp)
 255:	8b 05 b5 3d 00 00    	mov    0x3db5(%rip),%eax        # 4010 <p_scanln>
 25b:	0f af 45 e8          	imul   -0x18(%rbp),%eax
 25f:	89 c2                	mov    %eax,%edx
 261:	8b 45 ec             	mov    -0x14(%rbp),%eax
 264:	01 d0                	add    %edx,%eax
 266:	89 45 f4             	mov    %eax,-0xc(%rbp)
 269:	8b 05 a9 3d 00 00    	mov    0x3da9(%rip),%eax        # 4018 <size>
 26f:	39 45 f4             	cmp    %eax,-0xc(%rbp)
 272:	7f 6e                	jg     2e2 <draw_pixel+0xa9>
 274:	8b 05 9a 3d 00 00    	mov    0x3d9a(%rip),%eax        # 4014 <p_format>
 27a:	83 f8 01             	cmp    $0x1,%eax
 27d:	75 40                	jne    2bf <draw_pixel+0x86>
 27f:	8b 45 e4             	mov    -0x1c(%rbp),%eax
 282:	c1 e0 10             	shl    $0x10,%eax
 285:	25 00 00 ff 00       	and    $0xff0000,%eax
 28a:	89 45 f0             	mov    %eax,-0x10(%rbp)
 28d:	8b 45 e4             	mov    -0x1c(%rbp),%eax
 290:	25 00 ff 00 00       	and    $0xff00,%eax
 295:	01 45 f0             	add    %eax,-0x10(%rbp)
 298:	8b 45 e4             	mov    -0x1c(%rbp),%eax
 29b:	c1 e8 10             	shr    $0x10,%eax
 29e:	0f b6 c0             	movzbl %al,%eax
 2a1:	01 45 f0             	add    %eax,-0x10(%rbp)
 2a4:	8b 45 f4             	mov    -0xc(%rbp),%eax
 2a7:	48 98                	cltq
 2a9:	48 8d 14 85 00 00 00 	lea    0x0(,%rax,4),%rdx
 2b0:	00 
 2b1:	48 8b 45 f8          	mov    -0x8(%rbp),%rax
 2b5:	48 01 c2             	add    %rax,%rdx
 2b8:	8b 45 f0             	mov    -0x10(%rbp),%eax
 2bb:	89 02                	mov    %eax,(%rdx)
 2bd:	eb 23                	jmp    2e2 <draw_pixel+0xa9>
 2bf:	8b 05 4f 3d 00 00    	mov    0x3d4f(%rip),%eax        # 4014 <p_format>
 2c5:	85 c0                	test   %eax,%eax
 2c7:	75 19                	jne    2e2 <draw_pixel+0xa9>
 2c9:	8b 45 f4             	mov    -0xc(%rbp),%eax
 2cc:	48 98                	cltq
 2ce:	48 8d 14 85 00 00 00 	lea    0x0(,%rax,4),%rdx
 2d5:	00 
 2d6:	48 8b 45 f8          	mov    -0x8(%rbp),%rax
 2da:	48 01 c2             	add    %rax,%rdx
 2dd:	8b 45 e4             	mov    -0x1c(%rbp),%eax
 2e0:	89 02                	mov    %eax,(%rdx)
 2e2:	90                   	nop
 2e3:	c9                   	leave
 2e4:	c3                   	ret

00000000000002e5 <draw_16color>:
 2e5:	55                   	push   %rbp
 2e6:	48 89 e5             	mov    %rsp,%rbp
 2e9:	48 83 ec 30          	sub    $0x30,%rsp
 2ed:	89 7d dc             	mov    %edi,-0x24(%rbp)
 2f0:	89 75 d8             	mov    %esi,-0x28(%rbp)
 2f3:	89 55 d4             	mov    %edx,-0x2c(%rbp)
 2f6:	48 8b 05 03 3d 00 00 	mov    0x3d03(%rip),%rax        # 4000 <fb_addr>
 2fd:	48 89 45 f8          	mov    %rax,-0x8(%rbp)
 301:	8b 05 09 3d 00 00    	mov    0x3d09(%rip),%eax        # 4010 <p_scanln>
 307:	0f af 45 d8          	imul   -0x28(%rbp),%eax
 30b:	89 c2                	mov    %eax,%edx
 30d:	8b 45 dc             	mov    -0x24(%rbp),%eax
 310:	01 d0                	add    %edx,%eax
 312:	89 45 f4             	mov    %eax,-0xc(%rbp)
 315:	8b 45 d4             	mov    -0x2c(%rbp),%eax
 318:	48 98                	cltq
 31a:	48 8d 14 85 00 00 00 	lea    0x0(,%rax,4),%rdx
 321:	00 
 322:	48 8d 05 d7 1c 00 00 	lea    0x1cd7(%rip),%rax        # 2000 <vga16_color>
 329:	8b 04 02             	mov    (%rdx,%rax,1),%eax
 32c:	89 45 f0             	mov    %eax,-0x10(%rbp)
 32f:	8b 05 e3 3c 00 00    	mov    0x3ce3(%rip),%eax        # 4018 <size>
 335:	39 45 f4             	cmp    %eax,-0xc(%rbp)
 338:	7f 6e                	jg     3a8 <draw_16color+0xc3>
 33a:	8b 05 d4 3c 00 00    	mov    0x3cd4(%rip),%eax        # 4014 <p_format>
 340:	83 f8 01             	cmp    $0x1,%eax
 343:	75 40                	jne    385 <draw_16color+0xa0>
 345:	8b 45 f0             	mov    -0x10(%rbp),%eax
 348:	c1 e0 10             	shl    $0x10,%eax
 34b:	25 00 00 ff 00       	and    $0xff0000,%eax
 350:	89 45 ec             	mov    %eax,-0x14(%rbp)
 353:	8b 45 f0             	mov    -0x10(%rbp),%eax
 356:	25 00 ff 00 00       	and    $0xff00,%eax
 35b:	01 45 ec             	add    %eax,-0x14(%rbp)
 35e:	8b 45 f0             	mov    -0x10(%rbp),%eax
 361:	c1 e8 10             	shr    $0x10,%eax
 364:	0f b6 c0             	movzbl %al,%eax
 367:	01 45 ec             	add    %eax,-0x14(%rbp)
 36a:	8b 45 f4             	mov    -0xc(%rbp),%eax
 36d:	48 98                	cltq
 36f:	48 8d 14 85 00 00 00 	lea    0x0(,%rax,4),%rdx
 376:	00 
 377:	48 8b 45 f8          	mov    -0x8(%rbp),%rax
 37b:	48 01 c2             	add    %rax,%rdx
 37e:	8b 45 ec             	mov    -0x14(%rbp),%eax
 381:	89 02                	mov    %eax,(%rdx)
 383:	eb 23                	jmp    3a8 <draw_16color+0xc3>
 385:	8b 05 89 3c 00 00    	mov    0x3c89(%rip),%eax        # 4014 <p_format>
 38b:	85 c0                	test   %eax,%eax
 38d:	75 19                	jne    3a8 <draw_16color+0xc3>
 38f:	8b 45 f4             	mov    -0xc(%rbp),%eax
 392:	48 98                	cltq
 394:	48 8d 14 85 00 00 00 	lea    0x0(,%rax,4),%rdx
 39b:	00 
 39c:	48 8b 45 f8          	mov    -0x8(%rbp),%rax
 3a0:	48 01 c2             	add    %rax,%rdx
 3a3:	8b 45 f0             	mov    -0x10(%rbp),%eax
 3a6:	89 02                	mov    %eax,(%rdx)
 3a8:	90                   	nop
 3a9:	c9                   	leave
 3aa:	c3                   	ret

00000000000003ab <Set_GlobalTextColor>:
 3ab:	55                   	push   %rbp
 3ac:	48 89 e5             	mov    %rsp,%rbp
 3af:	48 83 ec 08          	sub    $0x8,%rsp
 3b3:	89 7d fc             	mov    %edi,-0x4(%rbp)
 3b6:	8b 45 fc             	mov    -0x4(%rbp),%eax
 3b9:	89 05 81 1c 00 00    	mov    %eax,0x1c81(%rip)        # 2040 <global_textcolor>
 3bf:	90                   	nop
 3c0:	c9                   	leave
 3c1:	c3                   	ret

00000000000003c2 <check_byte>:
 3c2:	55                   	push   %rbp
 3c3:	48 89 e5             	mov    %rsp,%rbp
 3c6:	48 83 ec 10          	sub    $0x10,%rsp
 3ca:	89 f1                	mov    %esi,%ecx
 3cc:	89 d0                	mov    %edx,%eax
 3ce:	89 fa                	mov    %edi,%edx
 3d0:	88 55 fc             	mov    %dl,-0x4(%rbp)
 3d3:	89 ca                	mov    %ecx,%edx
 3d5:	88 55 f8             	mov    %dl,-0x8(%rbp)
 3d8:	88 45 f4             	mov    %al,-0xc(%rbp)
 3db:	80 7d f4 01          	cmpb   $0x1,-0xc(%rbp)
 3df:	75 1a                	jne    3fb <check_byte+0x39>
 3e1:	0f b6 75 fc          	movzbl -0x4(%rbp),%esi
 3e5:	0f b6 45 f8          	movzbl -0x8(%rbp),%eax
 3e9:	ba 07 00 00 00       	mov    $0x7,%edx
 3ee:	29 c2                	sub    %eax,%edx
 3f0:	89 f0                	mov    %esi,%eax
 3f2:	89 d1                	mov    %edx,%ecx
 3f4:	d3 f8                	sar    %cl,%eax
 3f6:	88 45 fc             	mov    %al,-0x4(%rbp)
 3f9:	eb 11                	jmp    40c <check_byte+0x4a>
 3fb:	0f b6 55 fc          	movzbl -0x4(%rbp),%edx
 3ff:	0f b6 45 f8          	movzbl -0x8(%rbp),%eax
 403:	89 c1                	mov    %eax,%ecx
 405:	d3 fa                	sar    %cl,%edx
 407:	89 d0                	mov    %edx,%eax
 409:	88 45 fc             	mov    %al,-0x4(%rbp)
 40c:	0f b6 45 fc          	movzbl -0x4(%rbp),%eax
 410:	83 e0 01             	and    $0x1,%eax
 413:	c9                   	leave
 414:	c3                   	ret

0000000000000415 <draw_char>:
 415:	55                   	push   %rbp
 416:	48 89 e5             	mov    %rsp,%rbp
 419:	48 83 ec 20          	sub    $0x20,%rsp
 41d:	89 f8                	mov    %edi,%eax
 41f:	89 75 e8             	mov    %esi,-0x18(%rbp)
 422:	89 55 e4             	mov    %edx,-0x1c(%rbp)
 425:	88 45 ec             	mov    %al,-0x14(%rbp)
 428:	c1 65 e8 03          	shll   $0x3,-0x18(%rbp)
 42c:	c1 65 e4 04          	shll   $0x4,-0x1c(%rbp)
 430:	0f be 45 ec          	movsbl -0x14(%rbp),%eax
 434:	c1 e0 04             	shl    $0x4,%eax
 437:	89 45 f4             	mov    %eax,-0xc(%rbp)
 43a:	c7 45 fc 00 00 00 00 	movl   $0x0,-0x4(%rbp)
 441:	e9 88 00 00 00       	jmp    4ce <draw_char+0xb9>
 446:	8b 55 f4             	mov    -0xc(%rbp),%edx
 449:	8b 45 fc             	mov    -0x4(%rbp),%eax
 44c:	01 d0                	add    %edx,%eax
 44e:	48 98                	cltq
 450:	48 8d 15 09 1c 00 00 	lea    0x1c09(%rip),%rdx        # 2060 <font_data>
 457:	0f b6 04 10          	movzbl (%rax,%rdx,1),%eax
 45b:	88 45 f3             	mov    %al,-0xd(%rbp)
 45e:	c6 45 fb 00          	movb   $0x0,-0x5(%rbp)
 462:	eb 58                	jmp    4bc <draw_char+0xa7>
 464:	0f b6 4d fb          	movzbl -0x5(%rbp),%ecx
 468:	0f b6 45 f3          	movzbl -0xd(%rbp),%eax
 46c:	ba 01 00 00 00       	mov    $0x1,%edx
 471:	89 ce                	mov    %ecx,%esi
 473:	89 c7                	mov    %eax,%edi
 475:	e8 48 ff ff ff       	call   3c2 <check_byte>
 47a:	88 45 f2             	mov    %al,-0xe(%rbp)
 47d:	80 7d f2 01          	cmpb   $0x1,-0xe(%rbp)
 481:	75 17                	jne    49a <draw_char+0x85>
 483:	8b 15 b7 1b 00 00    	mov    0x1bb7(%rip),%edx        # 2040 <global_textcolor>
 489:	8b 4d e4             	mov    -0x1c(%rbp),%ecx
 48c:	8b 45 e8             	mov    -0x18(%rbp),%eax
 48f:	89 ce                	mov    %ecx,%esi
 491:	89 c7                	mov    %eax,%edi
 493:	e8 4d fe ff ff       	call   2e5 <draw_16color>
 498:	eb 14                	jmp    4ae <draw_char+0x99>
 49a:	8b 4d e4             	mov    -0x1c(%rbp),%ecx
 49d:	8b 45 e8             	mov    -0x18(%rbp),%eax
 4a0:	ba 00 00 00 00       	mov    $0x0,%edx
 4a5:	89 ce                	mov    %ecx,%esi
 4a7:	89 c7                	mov    %eax,%edi
 4a9:	e8 37 fe ff ff       	call   2e5 <draw_16color>
 4ae:	83 45 e8 01          	addl   $0x1,-0x18(%rbp)
 4b2:	0f b6 45 fb          	movzbl -0x5(%rbp),%eax
 4b6:	83 c0 01             	add    $0x1,%eax
 4b9:	88 45 fb             	mov    %al,-0x5(%rbp)
 4bc:	80 7d fb 07          	cmpb   $0x7,-0x5(%rbp)
 4c0:	76 a2                	jbe    464 <draw_char+0x4f>
 4c2:	83 6d e8 08          	subl   $0x8,-0x18(%rbp)
 4c6:	83 45 e4 01          	addl   $0x1,-0x1c(%rbp)
 4ca:	83 45 fc 01          	addl   $0x1,-0x4(%rbp)
 4ce:	83 7d fc 0f          	cmpl   $0xf,-0x4(%rbp)
 4d2:	0f 8e 6e ff ff ff    	jle    446 <draw_char+0x31>
 4d8:	90                   	nop
 4d9:	90                   	nop
 4da:	c9                   	leave
 4db:	c3                   	ret

00000000000004dc <draw_cursor>:
 4dc:	55                   	push   %rbp
 4dd:	48 89 e5             	mov    %rsp,%rbp
 4e0:	48 83 ec 18          	sub    $0x18,%rsp
 4e4:	89 7d ec             	mov    %edi,-0x14(%rbp)
 4e7:	8b 05 33 3b 00 00    	mov    0x3b33(%rip),%eax        # 4020 <cursor>
 4ed:	c1 e0 03             	shl    $0x3,%eax
 4f0:	89 45 f4             	mov    %eax,-0xc(%rbp)
 4f3:	8b 05 2b 3b 00 00    	mov    0x3b2b(%rip),%eax        # 4024 <cursor+0x4>
 4f9:	c1 e0 04             	shl    $0x4,%eax
 4fc:	89 45 f0             	mov    %eax,-0x10(%rbp)
 4ff:	c7 45 fc 00 00 00 00 	movl   $0x0,-0x4(%rbp)
 506:	eb 35                	jmp    53d <draw_cursor+0x61>
 508:	c7 45 f8 00 00 00 00 	movl   $0x0,-0x8(%rbp)
 50f:	eb 22                	jmp    533 <draw_cursor+0x57>
 511:	8b 55 f0             	mov    -0x10(%rbp),%edx
 514:	8b 45 fc             	mov    -0x4(%rbp),%eax
 517:	8d 34 02             	lea    (%rdx,%rax,1),%esi
 51a:	8b 55 f4             	mov    -0xc(%rbp),%edx
 51d:	8b 45 f8             	mov    -0x8(%rbp),%eax
 520:	8d 0c 02             	lea    (%rdx,%rax,1),%ecx
 523:	8b 45 ec             	mov    -0x14(%rbp),%eax
 526:	89 c2                	mov    %eax,%edx
 528:	89 cf                	mov    %ecx,%edi
 52a:	e8 b6 fd ff ff       	call   2e5 <draw_16color>
 52f:	83 45 f8 01          	addl   $0x1,-0x8(%rbp)
 533:	83 7d f8 07          	cmpl   $0x7,-0x8(%rbp)
 537:	7e d8                	jle    511 <draw_cursor+0x35>
 539:	83 45 fc 01          	addl   $0x1,-0x4(%rbp)
 53d:	83 7d fc 0f          	cmpl   $0xf,-0x4(%rbp)
 541:	7e c5                	jle    508 <draw_cursor+0x2c>
 543:	90                   	nop
 544:	90                   	nop
 545:	c9                   	leave
 546:	c3                   	ret

0000000000000547 <cursor_inc>:
 547:	55                   	push   %rbp
 548:	48 89 e5             	mov    %rsp,%rbp
 54b:	8b 05 cf 3a 00 00    	mov    0x3acf(%rip),%eax        # 4020 <cursor>
 551:	83 c0 01             	add    $0x1,%eax
 554:	89 05 c6 3a 00 00    	mov    %eax,0x3ac6(%rip)        # 4020 <cursor>
 55a:	8b 15 c0 3a 00 00    	mov    0x3ac0(%rip),%edx        # 4020 <cursor>
 560:	8b 05 aa 3a 00 00    	mov    0x3aaa(%rip),%eax        # 4010 <p_scanln>
 566:	c1 f8 03             	sar    $0x3,%eax
 569:	39 c2                	cmp    %eax,%edx
 56b:	7c 19                	jl     586 <cursor_inc+0x3f>
 56d:	c7 05 a9 3a 00 00 00 	movl   $0x0,0x3aa9(%rip)        # 4020 <cursor>
 574:	00 00 00 
 577:	8b 05 a7 3a 00 00    	mov    0x3aa7(%rip),%eax        # 4024 <cursor+0x4>
 57d:	83 c0 01             	add    $0x1,%eax
 580:	89 05 9e 3a 00 00    	mov    %eax,0x3a9e(%rip)        # 4024 <cursor+0x4>
 586:	90                   	nop
 587:	5d                   	pop    %rbp
 588:	c3                   	ret

0000000000000589 <print_unsigned>:
 589:	55                   	push   %rbp
 58a:	48 89 e5             	mov    %rsp,%rbp
 58d:	48 83 ec 18          	sub    $0x18,%rsp
 591:	89 7d ec             	mov    %edi,-0x14(%rbp)
 594:	c7 45 fc 00 00 00 00 	movl   $0x0,-0x4(%rbp)
 59b:	8b 4d ec             	mov    -0x14(%rbp),%ecx
 59e:	89 ca                	mov    %ecx,%edx
 5a0:	b8 cd cc cc cc       	mov    $0xcccccccd,%eax
 5a5:	48 0f af c2          	imul   %rdx,%rax
 5a9:	48 c1 e8 20          	shr    $0x20,%rax
 5ad:	89 c2                	mov    %eax,%edx
 5af:	c1 ea 03             	shr    $0x3,%edx
 5b2:	89 d0                	mov    %edx,%eax
 5b4:	c1 e0 02             	shl    $0x2,%eax
 5b7:	01 d0                	add    %edx,%eax
 5b9:	01 c0                	add    %eax,%eax
 5bb:	29 c1                	sub    %eax,%ecx
 5bd:	89 ca                	mov    %ecx,%edx
 5bf:	89 d0                	mov    %edx,%eax
 5c1:	8d 48 30             	lea    0x30(%rax),%ecx
 5c4:	8b 45 fc             	mov    -0x4(%rbp),%eax
 5c7:	8d 50 01             	lea    0x1(%rax),%edx
 5ca:	89 55 fc             	mov    %edx,-0x4(%rbp)
 5cd:	89 ca                	mov    %ecx,%edx
 5cf:	48 98                	cltq
 5d1:	88 54 05 f2          	mov    %dl,-0xe(%rbp,%rax,1)
 5d5:	8b 45 ec             	mov    -0x14(%rbp),%eax
 5d8:	89 c2                	mov    %eax,%edx
 5da:	b8 cd cc cc cc       	mov    $0xcccccccd,%eax
 5df:	48 0f af c2          	imul   %rdx,%rax
 5e3:	48 c1 e8 20          	shr    $0x20,%rax
 5e7:	c1 e8 03             	shr    $0x3,%eax
 5ea:	89 45 ec             	mov    %eax,-0x14(%rbp)
 5ed:	83 7d ec 00          	cmpl   $0x0,-0x14(%rbp)
 5f1:	75 a8                	jne    59b <print_unsigned+0x12>
 5f3:	eb 2b                	jmp    620 <print_unsigned+0x97>
 5f5:	8b 15 29 3a 00 00    	mov    0x3a29(%rip),%edx        # 4024 <cursor+0x4>
 5fb:	8b 0d 1f 3a 00 00    	mov    0x3a1f(%rip),%ecx        # 4020 <cursor>
 601:	83 6d fc 01          	subl   $0x1,-0x4(%rbp)
 605:	8b 45 fc             	mov    -0x4(%rbp),%eax
 608:	48 98                	cltq
 60a:	0f b6 44 05 f2       	movzbl -0xe(%rbp,%rax,1),%eax
 60f:	0f be c0             	movsbl %al,%eax
 612:	89 ce                	mov    %ecx,%esi
 614:	89 c7                	mov    %eax,%edi
 616:	e8 fa fd ff ff       	call   415 <draw_char>
 61b:	e8 27 ff ff ff       	call   547 <cursor_inc>
 620:	83 7d fc 00          	cmpl   $0x0,-0x4(%rbp)
 624:	7f cf                	jg     5f5 <print_unsigned+0x6c>
 626:	90                   	nop
 627:	90                   	nop
 628:	c9                   	leave
 629:	c3                   	ret

000000000000062a <print_unsigned_long>:
 62a:	55                   	push   %rbp
 62b:	48 89 e5             	mov    %rsp,%rbp
 62e:	48 83 ec 30          	sub    $0x30,%rsp
 632:	48 89 7d d8          	mov    %rdi,-0x28(%rbp)
 636:	c7 45 fc 00 00 00 00 	movl   $0x0,-0x4(%rbp)
 63d:	48 8b 4d d8          	mov    -0x28(%rbp),%rcx
 641:	48 ba cd cc cc cc cc 	movabs $0xcccccccccccccccd,%rdx
 648:	cc cc cc 
 64b:	48 89 c8             	mov    %rcx,%rax
 64e:	48 f7 e2             	mul    %rdx
 651:	48 c1 ea 03          	shr    $0x3,%rdx
 655:	48 89 d0             	mov    %rdx,%rax
 658:	48 c1 e0 02          	shl    $0x2,%rax
 65c:	48 01 d0             	add    %rdx,%rax
 65f:	48 01 c0             	add    %rax,%rax
 662:	48 29 c1             	sub    %rax,%rcx
 665:	48 89 ca             	mov    %rcx,%rdx
 668:	89 d0                	mov    %edx,%eax
 66a:	8d 48 30             	lea    0x30(%rax),%ecx
 66d:	8b 45 fc             	mov    -0x4(%rbp),%eax
 670:	8d 50 01             	lea    0x1(%rax),%edx
 673:	89 55 fc             	mov    %edx,-0x4(%rbp)
 676:	89 ca                	mov    %ecx,%edx
 678:	48 98                	cltq
 67a:	88 54 05 e0          	mov    %dl,-0x20(%rbp,%rax,1)
 67e:	48 8b 45 d8          	mov    -0x28(%rbp),%rax
 682:	48 ba cd cc cc cc cc 	movabs $0xcccccccccccccccd,%rdx
 689:	cc cc cc 
 68c:	48 f7 e2             	mul    %rdx
 68f:	48 89 d0             	mov    %rdx,%rax
 692:	48 c1 e8 03          	shr    $0x3,%rax
 696:	48 89 45 d8          	mov    %rax,-0x28(%rbp)
 69a:	48 83 7d d8 00       	cmpq   $0x0,-0x28(%rbp)
 69f:	75 9c                	jne    63d <print_unsigned_long+0x13>
 6a1:	eb 26                	jmp    6c9 <print_unsigned_long+0x9f>
 6a3:	8b 15 7b 39 00 00    	mov    0x397b(%rip),%edx        # 4024 <cursor+0x4>
 6a9:	8b 0d 71 39 00 00    	mov    0x3971(%rip),%ecx        # 4020 <cursor>
 6af:	83 6d fc 01          	subl   $0x1,-0x4(%rbp)
 6b3:	8b 45 fc             	mov    -0x4(%rbp),%eax
 6b6:	48 98                	cltq
 6b8:	0f b6 44 05 e0       	movzbl -0x20(%rbp,%rax,1),%eax
 6bd:	0f be c0             	movsbl %al,%eax
 6c0:	89 ce                	mov    %ecx,%esi
 6c2:	89 c7                	mov    %eax,%edi
 6c4:	e8 4c fd ff ff       	call   415 <draw_char>
 6c9:	83 7d fc 00          	cmpl   $0x0,-0x4(%rbp)
 6cd:	7f d4                	jg     6a3 <print_unsigned_long+0x79>
 6cf:	e8 73 fe ff ff       	call   547 <cursor_inc>
 6d4:	90                   	nop
 6d5:	c9                   	leave
 6d6:	c3                   	ret

00000000000006d7 <print_int>:
 6d7:	55                   	push   %rbp
 6d8:	48 89 e5             	mov    %rsp,%rbp
 6db:	48 83 ec 18          	sub    $0x18,%rsp
 6df:	89 7d ec             	mov    %edi,-0x14(%rbp)
 6e2:	83 7d ec 00          	cmpl   $0x0,-0x14(%rbp)
 6e6:	79 31                	jns    719 <print_int+0x42>
 6e8:	8b 15 36 39 00 00    	mov    0x3936(%rip),%edx        # 4024 <cursor+0x4>
 6ee:	8b 05 2c 39 00 00    	mov    0x392c(%rip),%eax        # 4020 <cursor>
 6f4:	89 c6                	mov    %eax,%esi
 6f6:	bf 2d 00 00 00       	mov    $0x2d,%edi
 6fb:	e8 15 fd ff ff       	call   415 <draw_char>
 700:	e8 42 fe ff ff       	call   547 <cursor_inc>
 705:	8b 45 ec             	mov    -0x14(%rbp),%eax
 708:	f7 d8                	neg    %eax
 70a:	89 45 fc             	mov    %eax,-0x4(%rbp)
 70d:	8b 45 fc             	mov    -0x4(%rbp),%eax
 710:	89 c7                	mov    %eax,%edi
 712:	e8 72 fe ff ff       	call   589 <print_unsigned>
 717:	eb 0a                	jmp    723 <print_int+0x4c>
 719:	8b 45 ec             	mov    -0x14(%rbp),%eax
 71c:	89 c7                	mov    %eax,%edi
 71e:	e8 66 fe ff ff       	call   589 <print_unsigned>
 723:	90                   	nop
 724:	c9                   	leave
 725:	c3                   	ret

0000000000000726 <print_long>:
 726:	55                   	push   %rbp
 727:	48 89 e5             	mov    %rsp,%rbp
 72a:	48 83 ec 20          	sub    $0x20,%rsp
 72e:	48 89 7d e8          	mov    %rdi,-0x18(%rbp)
 732:	48 83 7d e8 00       	cmpq   $0x0,-0x18(%rbp)
 737:	79 36                	jns    76f <print_long+0x49>
 739:	8b 15 e5 38 00 00    	mov    0x38e5(%rip),%edx        # 4024 <cursor+0x4>
 73f:	8b 05 db 38 00 00    	mov    0x38db(%rip),%eax        # 4020 <cursor>
 745:	89 c6                	mov    %eax,%esi
 747:	bf 2d 00 00 00       	mov    $0x2d,%edi
 74c:	e8 c4 fc ff ff       	call   415 <draw_char>
 751:	e8 f1 fd ff ff       	call   547 <cursor_inc>
 756:	48 8b 45 e8          	mov    -0x18(%rbp),%rax
 75a:	48 f7 d8             	neg    %rax
 75d:	48 89 45 f8          	mov    %rax,-0x8(%rbp)
 761:	48 8b 45 f8          	mov    -0x8(%rbp),%rax
 765:	48 89 c7             	mov    %rax,%rdi
 768:	e8 bd fe ff ff       	call   62a <print_unsigned_long>
 76d:	eb 0c                	jmp    77b <print_long+0x55>
 76f:	48 8b 45 e8          	mov    -0x18(%rbp),%rax
 773:	48 89 c7             	mov    %rax,%rdi
 776:	e8 af fe ff ff       	call   62a <print_unsigned_long>
 77b:	90                   	nop
 77c:	c9                   	leave
 77d:	c3                   	ret

000000000000077e <print_byte_hex>:
 77e:	55                   	push   %rbp
 77f:	48 89 e5             	mov    %rsp,%rbp
 782:	48 83 ec 30          	sub    $0x30,%rsp
 786:	89 f8                	mov    %edi,%eax
 788:	88 45 dc             	mov    %al,-0x24(%rbp)
 78b:	48 b8 30 31 32 33 34 	movabs $0x3736353433323130,%rax
 792:	35 36 37 
 795:	48 ba 38 39 41 42 43 	movabs $0x4645444342413938,%rdx
 79c:	44 45 46 
 79f:	48 89 45 e0          	mov    %rax,-0x20(%rbp)
 7a3:	48 89 55 e8          	mov    %rdx,-0x18(%rbp)
 7a7:	c6 45 f0 00          	movb   $0x0,-0x10(%rbp)
 7ab:	0f b6 45 dc          	movzbl -0x24(%rbp),%eax
 7af:	c0 e8 04             	shr    $0x4,%al
 7b2:	0f b6 c0             	movzbl %al,%eax
 7b5:	83 e0 0f             	and    $0xf,%eax
 7b8:	48 98                	cltq
 7ba:	0f b6 44 05 e0       	movzbl -0x20(%rbp,%rax,1),%eax
 7bf:	88 45 ff             	mov    %al,-0x1(%rbp)
 7c2:	0f b6 45 dc          	movzbl -0x24(%rbp),%eax
 7c6:	83 e0 0f             	and    $0xf,%eax
 7c9:	48 98                	cltq
 7cb:	0f b6 44 05 e0       	movzbl -0x20(%rbp,%rax,1),%eax
 7d0:	88 45 fe             	mov    %al,-0x2(%rbp)
 7d3:	8b 15 4b 38 00 00    	mov    0x384b(%rip),%edx        # 4024 <cursor+0x4>
 7d9:	8b 0d 41 38 00 00    	mov    0x3841(%rip),%ecx        # 4020 <cursor>
 7df:	0f be 45 ff          	movsbl -0x1(%rbp),%eax
 7e3:	89 ce                	mov    %ecx,%esi
 7e5:	89 c7                	mov    %eax,%edi
 7e7:	e8 29 fc ff ff       	call   415 <draw_char>
 7ec:	e8 56 fd ff ff       	call   547 <cursor_inc>
 7f1:	8b 15 2d 38 00 00    	mov    0x382d(%rip),%edx        # 4024 <cursor+0x4>
 7f7:	8b 0d 23 38 00 00    	mov    0x3823(%rip),%ecx        # 4020 <cursor>
 7fd:	0f be 45 fe          	movsbl -0x2(%rbp),%eax
 801:	89 ce                	mov    %ecx,%esi
 803:	89 c7                	mov    %eax,%edi
 805:	e8 0b fc ff ff       	call   415 <draw_char>
 80a:	e8 38 fd ff ff       	call   547 <cursor_inc>
 80f:	90                   	nop
 810:	c9                   	leave
 811:	c3                   	ret

0000000000000812 <print_word_hex>:
 812:	55                   	push   %rbp
 813:	48 89 e5             	mov    %rsp,%rbp
 816:	48 83 ec 10          	sub    $0x10,%rsp
 81a:	89 f8                	mov    %edi,%eax
 81c:	66 89 45 fc          	mov    %ax,-0x4(%rbp)
 820:	0f b7 45 fc          	movzwl -0x4(%rbp),%eax
 824:	66 c1 e8 08          	shr    $0x8,%ax
 828:	0f b6 c0             	movzbl %al,%eax
 82b:	89 c7                	mov    %eax,%edi
 82d:	e8 4c ff ff ff       	call   77e <print_byte_hex>
 832:	0f b7 45 fc          	movzwl -0x4(%rbp),%eax
 836:	0f b6 c0             	movzbl %al,%eax
 839:	89 c7                	mov    %eax,%edi
 83b:	e8 3e ff ff ff       	call   77e <print_byte_hex>
 840:	90                   	nop
 841:	c9                   	leave
 842:	c3                   	ret

0000000000000843 <print_dword_hex>:
 843:	55                   	push   %rbp
 844:	48 89 e5             	mov    %rsp,%rbp
 847:	48 83 ec 10          	sub    $0x10,%rsp
 84b:	89 7d fc             	mov    %edi,-0x4(%rbp)
 84e:	8b 45 fc             	mov    -0x4(%rbp),%eax
 851:	c1 e8 10             	shr    $0x10,%eax
 854:	0f b7 c0             	movzwl %ax,%eax
 857:	89 c7                	mov    %eax,%edi
 859:	e8 b4 ff ff ff       	call   812 <print_word_hex>
 85e:	8b 45 fc             	mov    -0x4(%rbp),%eax
 861:	0f b7 c0             	movzwl %ax,%eax
 864:	89 c7                	mov    %eax,%edi
 866:	e8 a7 ff ff ff       	call   812 <print_word_hex>
 86b:	90                   	nop
 86c:	c9                   	leave
 86d:	c3                   	ret

000000000000086e <print_qword_hex>:
 86e:	55                   	push   %rbp
 86f:	48 89 e5             	mov    %rsp,%rbp
 872:	48 83 ec 10          	sub    $0x10,%rsp
 876:	48 89 7d f8          	mov    %rdi,-0x8(%rbp)
 87a:	48 8b 45 f8          	mov    -0x8(%rbp),%rax
 87e:	48 c1 e8 20          	shr    $0x20,%rax
 882:	89 c7                	mov    %eax,%edi
 884:	e8 ba ff ff ff       	call   843 <print_dword_hex>
 889:	48 8b 45 f8          	mov    -0x8(%rbp),%rax
 88d:	89 c7                	mov    %eax,%edi
 88f:	e8 af ff ff ff       	call   843 <print_dword_hex>
 894:	90                   	nop
 895:	c9                   	leave
 896:	c3                   	ret

0000000000000897 <printf>:
 897:	55                   	push   %rbp
 898:	48 89 e5             	mov    %rsp,%rbp
 89b:	48 81 ec f0 00 00 00 	sub    $0xf0,%rsp
 8a2:	48 89 bd 18 ff ff ff 	mov    %rdi,-0xe8(%rbp)
 8a9:	48 89 b5 58 ff ff ff 	mov    %rsi,-0xa8(%rbp)
 8b0:	48 89 95 60 ff ff ff 	mov    %rdx,-0xa0(%rbp)
 8b7:	48 89 8d 68 ff ff ff 	mov    %rcx,-0x98(%rbp)
 8be:	4c 89 85 70 ff ff ff 	mov    %r8,-0x90(%rbp)
 8c5:	4c 89 8d 78 ff ff ff 	mov    %r9,-0x88(%rbp)
 8cc:	84 c0                	test   %al,%al
 8ce:	74 20                	je     8f0 <printf+0x59>
 8d0:	0f 29 45 80          	movaps %xmm0,-0x80(%rbp)
 8d4:	0f 29 4d 90          	movaps %xmm1,-0x70(%rbp)
 8d8:	0f 29 55 a0          	movaps %xmm2,-0x60(%rbp)
 8dc:	0f 29 5d b0          	movaps %xmm3,-0x50(%rbp)
 8e0:	0f 29 65 c0          	movaps %xmm4,-0x40(%rbp)
 8e4:	0f 29 6d d0          	movaps %xmm5,-0x30(%rbp)
 8e8:	0f 29 75 e0          	movaps %xmm6,-0x20(%rbp)
 8ec:	0f 29 7d f0          	movaps %xmm7,-0x10(%rbp)
 8f0:	c7 85 30 ff ff ff 08 	movl   $0x8,-0xd0(%rbp)
 8f7:	00 00 00 
 8fa:	c7 85 34 ff ff ff 30 	movl   $0x30,-0xcc(%rbp)
 901:	00 00 00 
 904:	48 8d 45 10          	lea    0x10(%rbp),%rax
 908:	48 89 85 38 ff ff ff 	mov    %rax,-0xc8(%rbp)
 90f:	48 8d 85 50 ff ff ff 	lea    -0xb0(%rbp),%rax
 916:	48 89 85 40 ff ff ff 	mov    %rax,-0xc0(%rbp)
 91d:	8b 05 fd 36 00 00    	mov    0x36fd(%rip),%eax        # 4020 <cursor>
 923:	89 85 28 ff ff ff    	mov    %eax,-0xd8(%rbp)
 929:	8b 05 f5 36 00 00    	mov    0x36f5(%rip),%eax        # 4024 <cursor+0x4>
 92f:	89 85 2c ff ff ff    	mov    %eax,-0xd4(%rbp)
 935:	48 8b 85 18 ff ff ff 	mov    -0xe8(%rbp),%rax
 93c:	48 89 85 48 ff ff ff 	mov    %rax,-0xb8(%rbp)
 943:	e9 ed 03 00 00       	jmp    d35 <printf+0x49e>
 948:	48 8b 85 48 ff ff ff 	mov    -0xb8(%rbp),%rax
 94f:	0f b6 00             	movzbl (%rax),%eax
 952:	3c 0a                	cmp    $0xa,%al
 954:	75 1e                	jne    974 <printf+0xdd>
 956:	c7 05 c0 36 00 00 00 	movl   $0x0,0x36c0(%rip)        # 4020 <cursor>
 95d:	00 00 00 
 960:	8b 05 be 36 00 00    	mov    0x36be(%rip),%eax        # 4024 <cursor+0x4>
 966:	83 c0 01             	add    $0x1,%eax
 969:	89 05 b5 36 00 00    	mov    %eax,0x36b5(%rip)        # 4024 <cursor+0x4>
 96f:	e9 b9 03 00 00       	jmp    d2d <printf+0x496>
 974:	48 8b 85 48 ff ff ff 	mov    -0xb8(%rbp),%rax
 97b:	0f b6 00             	movzbl (%rax),%eax
 97e:	3c 25                	cmp    $0x25,%al
 980:	0f 85 80 03 00 00    	jne    d06 <printf+0x46f>
 986:	48 83 85 48 ff ff ff 	addq   $0x1,-0xb8(%rbp)
 98d:	01 
 98e:	48 8b 85 48 ff ff ff 	mov    -0xb8(%rbp),%rax
 995:	0f b6 00             	movzbl (%rax),%eax
 998:	0f be c0             	movsbl %al,%eax
 99b:	83 e8 62             	sub    $0x62,%eax
 99e:	83 f8 15             	cmp    $0x15,%eax
 9a1:	0f 87 2e 03 00 00    	ja     cd5 <printf+0x43e>
 9a7:	89 c0                	mov    %eax,%eax
 9a9:	48 8d 14 85 00 00 00 	lea    0x0(,%rax,4),%rdx
 9b0:	00 
 9b1:	48 8d 05 c8 06 00 00 	lea    0x6c8(%rip),%rax        # 1080 <cursor_Setpos+0x2d0>
 9b8:	8b 04 02             	mov    (%rdx,%rax,1),%eax
 9bb:	48 98                	cltq
 9bd:	48 8d 15 bc 06 00 00 	lea    0x6bc(%rip),%rdx        # 1080 <cursor_Setpos+0x2d0>
 9c4:	48 01 d0             	add    %rdx,%rax
 9c7:	ff e0                	jmp    *%rax
 9c9:	8b 85 30 ff ff ff    	mov    -0xd0(%rbp),%eax
 9cf:	83 f8 2f             	cmp    $0x2f,%eax
 9d2:	77 23                	ja     9f7 <printf+0x160>
 9d4:	48 8b 85 40 ff ff ff 	mov    -0xc0(%rbp),%rax
 9db:	8b 95 30 ff ff ff    	mov    -0xd0(%rbp),%edx
 9e1:	89 d2                	mov    %edx,%edx
 9e3:	48 01 d0             	add    %rdx,%rax
 9e6:	8b 95 30 ff ff ff    	mov    -0xd0(%rbp),%edx
 9ec:	83 c2 08             	add    $0x8,%edx
 9ef:	89 95 30 ff ff ff    	mov    %edx,-0xd0(%rbp)
 9f5:	eb 12                	jmp    a09 <printf+0x172>
 9f7:	48 8b 85 38 ff ff ff 	mov    -0xc8(%rbp),%rax
 9fe:	48 8d 50 08          	lea    0x8(%rax),%rdx
 a02:	48 89 95 38 ff ff ff 	mov    %rdx,-0xc8(%rbp)
 a09:	8b 00                	mov    (%rax),%eax
 a0b:	89 c7                	mov    %eax,%edi
 a0d:	e8 c5 fc ff ff       	call   6d7 <print_int>
 a12:	e9 16 03 00 00       	jmp    d2d <printf+0x496>
 a17:	48 8b 85 48 ff ff ff 	mov    -0xb8(%rbp),%rax
 a1e:	48 83 c0 01          	add    $0x1,%rax
 a22:	0f b6 00             	movzbl (%rax),%eax
 a25:	3c 78                	cmp    $0x78,%al
 a27:	75 56                	jne    a7f <printf+0x1e8>
 a29:	8b 85 30 ff ff ff    	mov    -0xd0(%rbp),%eax
 a2f:	83 f8 2f             	cmp    $0x2f,%eax
 a32:	77 23                	ja     a57 <printf+0x1c0>
 a34:	48 8b 85 40 ff ff ff 	mov    -0xc0(%rbp),%rax
 a3b:	8b 95 30 ff ff ff    	mov    -0xd0(%rbp),%edx
 a41:	89 d2                	mov    %edx,%edx
 a43:	48 01 d0             	add    %rdx,%rax
 a46:	8b 95 30 ff ff ff    	mov    -0xd0(%rbp),%edx
 a4c:	83 c2 08             	add    $0x8,%edx
 a4f:	89 95 30 ff ff ff    	mov    %edx,-0xd0(%rbp)
 a55:	eb 12                	jmp    a69 <printf+0x1d2>
 a57:	48 8b 85 38 ff ff ff 	mov    -0xc8(%rbp),%rax
 a5e:	48 8d 50 08          	lea    0x8(%rax),%rdx
 a62:	48 89 95 38 ff ff ff 	mov    %rdx,-0xc8(%rbp)
 a69:	8b 00                	mov    (%rax),%eax
 a6b:	89 c7                	mov    %eax,%edi
 a6d:	e8 d1 fd ff ff       	call   843 <print_dword_hex>
 a72:	48 83 85 48 ff ff ff 	addq   $0x1,-0xb8(%rbp)
 a79:	01 
 a7a:	e9 ae 02 00 00       	jmp    d2d <printf+0x496>
 a7f:	8b 85 30 ff ff ff    	mov    -0xd0(%rbp),%eax
 a85:	83 f8 2f             	cmp    $0x2f,%eax
 a88:	77 23                	ja     aad <printf+0x216>
 a8a:	48 8b 85 40 ff ff ff 	mov    -0xc0(%rbp),%rax
 a91:	8b 95 30 ff ff ff    	mov    -0xd0(%rbp),%edx
 a97:	89 d2                	mov    %edx,%edx
 a99:	48 01 d0             	add    %rdx,%rax
 a9c:	8b 95 30 ff ff ff    	mov    -0xd0(%rbp),%edx
 aa2:	83 c2 08             	add    $0x8,%edx
 aa5:	89 95 30 ff ff ff    	mov    %edx,-0xd0(%rbp)
 aab:	eb 12                	jmp    abf <printf+0x228>
 aad:	48 8b 85 38 ff ff ff 	mov    -0xc8(%rbp),%rax
 ab4:	48 8d 50 08          	lea    0x8(%rax),%rdx
 ab8:	48 89 95 38 ff ff ff 	mov    %rdx,-0xc8(%rbp)
 abf:	8b 00                	mov    (%rax),%eax
 ac1:	89 c7                	mov    %eax,%edi
 ac3:	e8 c1 fa ff ff       	call   589 <print_unsigned>
 ac8:	e9 60 02 00 00       	jmp    d2d <printf+0x496>
 acd:	48 83 85 48 ff ff ff 	addq   $0x1,-0xb8(%rbp)
 ad4:	01 
 ad5:	48 8b 85 48 ff ff ff 	mov    -0xb8(%rbp),%rax
 adc:	0f b6 00             	movzbl (%rax),%eax
 adf:	3c 64                	cmp    $0x64,%al
 ae1:	75 50                	jne    b33 <printf+0x29c>
 ae3:	8b 85 30 ff ff ff    	mov    -0xd0(%rbp),%eax
 ae9:	83 f8 2f             	cmp    $0x2f,%eax
 aec:	77 23                	ja     b11 <printf+0x27a>
 aee:	48 8b 85 40 ff ff ff 	mov    -0xc0(%rbp),%rax
 af5:	8b 95 30 ff ff ff    	mov    -0xd0(%rbp),%edx
 afb:	89 d2                	mov    %edx,%edx
 afd:	48 01 d0             	add    %rdx,%rax
 b00:	8b 95 30 ff ff ff    	mov    -0xd0(%rbp),%edx
 b06:	83 c2 08             	add    $0x8,%edx
 b09:	89 95 30 ff ff ff    	mov    %edx,-0xd0(%rbp)
 b0f:	eb 12                	jmp    b23 <printf+0x28c>
 b11:	48 8b 85 38 ff ff ff 	mov    -0xc8(%rbp),%rax
 b18:	48 8d 50 08          	lea    0x8(%rax),%rdx
 b1c:	48 89 95 38 ff ff ff 	mov    %rdx,-0xc8(%rbp)
 b23:	48 8b 00             	mov    (%rax),%rax
 b26:	48 89 c7             	mov    %rax,%rdi
 b29:	e8 f8 fb ff ff       	call   726 <print_long>
 b2e:	e9 fa 01 00 00       	jmp    d2d <printf+0x496>
 b33:	48 8b 85 48 ff ff ff 	mov    -0xb8(%rbp),%rax
 b3a:	0f b6 00             	movzbl (%rax),%eax
 b3d:	3c 75                	cmp    $0x75,%al
 b3f:	75 50                	jne    b91 <printf+0x2fa>
 b41:	8b 85 30 ff ff ff    	mov    -0xd0(%rbp),%eax
 b47:	83 f8 2f             	cmp    $0x2f,%eax
 b4a:	77 23                	ja     b6f <printf+0x2d8>
 b4c:	48 8b 85 40 ff ff ff 	mov    -0xc0(%rbp),%rax
 b53:	8b 95 30 ff ff ff    	mov    -0xd0(%rbp),%edx
 b59:	89 d2                	mov    %edx,%edx
 b5b:	48 01 d0             	add    %rdx,%rax
 b5e:	8b 95 30 ff ff ff    	mov    -0xd0(%rbp),%edx
 b64:	83 c2 08             	add    $0x8,%edx
 b67:	89 95 30 ff ff ff    	mov    %edx,-0xd0(%rbp)
 b6d:	eb 12                	jmp    b81 <printf+0x2ea>
 b6f:	48 8b 85 38 ff ff ff 	mov    -0xc8(%rbp),%rax
 b76:	48 8d 50 08          	lea    0x8(%rax),%rdx
 b7a:	48 89 95 38 ff ff ff 	mov    %rdx,-0xc8(%rbp)
 b81:	48 8b 00             	mov    (%rax),%rax
 b84:	48 89 c7             	mov    %rax,%rdi
 b87:	e8 9e fa ff ff       	call   62a <print_unsigned_long>
 b8c:	e9 9c 01 00 00       	jmp    d2d <printf+0x496>
 b91:	48 8b 85 48 ff ff ff 	mov    -0xb8(%rbp),%rax
 b98:	0f b6 00             	movzbl (%rax),%eax
 b9b:	3c 78                	cmp    $0x78,%al
 b9d:	75 50                	jne    bef <printf+0x358>
 b9f:	8b 85 30 ff ff ff    	mov    -0xd0(%rbp),%eax
 ba5:	83 f8 2f             	cmp    $0x2f,%eax
 ba8:	77 23                	ja     bcd <printf+0x336>
 baa:	48 8b 85 40 ff ff ff 	mov    -0xc0(%rbp),%rax
 bb1:	8b 95 30 ff ff ff    	mov    -0xd0(%rbp),%edx
 bb7:	89 d2                	mov    %edx,%edx
 bb9:	48 01 d0             	add    %rdx,%rax
 bbc:	8b 95 30 ff ff ff    	mov    -0xd0(%rbp),%edx
 bc2:	83 c2 08             	add    $0x8,%edx
 bc5:	89 95 30 ff ff ff    	mov    %edx,-0xd0(%rbp)
 bcb:	eb 12                	jmp    bdf <printf+0x348>
 bcd:	48 8b 85 38 ff ff ff 	mov    -0xc8(%rbp),%rax
 bd4:	48 8d 50 08          	lea    0x8(%rax),%rdx
 bd8:	48 89 95 38 ff ff ff 	mov    %rdx,-0xc8(%rbp)
 bdf:	48 8b 00             	mov    (%rax),%rax
 be2:	48 89 c7             	mov    %rax,%rdi
 be5:	e8 84 fc ff ff       	call   86e <print_qword_hex>
 bea:	e9 3e 01 00 00       	jmp    d2d <printf+0x496>
 bef:	8b 15 2f 34 00 00    	mov    0x342f(%rip),%edx        # 4024 <cursor+0x4>
 bf5:	8b 05 25 34 00 00    	mov    0x3425(%rip),%eax        # 4020 <cursor>
 bfb:	89 c6                	mov    %eax,%esi
 bfd:	bf 25 00 00 00       	mov    $0x25,%edi
 c02:	e8 0e f8 ff ff       	call   415 <draw_char>
 c07:	e8 3b f9 ff ff       	call   547 <cursor_inc>
 c0c:	8b 15 12 34 00 00    	mov    0x3412(%rip),%edx        # 4024 <cursor+0x4>
 c12:	8b 05 08 34 00 00    	mov    0x3408(%rip),%eax        # 4020 <cursor>
 c18:	89 c6                	mov    %eax,%esi
 c1a:	bf 6c 00 00 00       	mov    $0x6c,%edi
 c1f:	e8 f1 f7 ff ff       	call   415 <draw_char>
 c24:	e8 1e f9 ff ff       	call   547 <cursor_inc>
 c29:	48 83 ad 48 ff ff ff 	subq   $0x1,-0xb8(%rbp)
 c30:	01 
 c31:	e9 f7 00 00 00       	jmp    d2d <printf+0x496>
 c36:	8b 85 30 ff ff ff    	mov    -0xd0(%rbp),%eax
 c3c:	83 f8 2f             	cmp    $0x2f,%eax
 c3f:	77 23                	ja     c64 <printf+0x3cd>
 c41:	48 8b 85 40 ff ff ff 	mov    -0xc0(%rbp),%rax
 c48:	8b 95 30 ff ff ff    	mov    -0xd0(%rbp),%edx
 c4e:	89 d2                	mov    %edx,%edx
 c50:	48 01 d0             	add    %rdx,%rax
 c53:	8b 95 30 ff ff ff    	mov    -0xd0(%rbp),%edx
 c59:	83 c2 08             	add    $0x8,%edx
 c5c:	89 95 30 ff ff ff    	mov    %edx,-0xd0(%rbp)
 c62:	eb 12                	jmp    c76 <printf+0x3df>
 c64:	48 8b 85 38 ff ff ff 	mov    -0xc8(%rbp),%rax
 c6b:	48 8d 50 08          	lea    0x8(%rax),%rdx
 c6f:	48 89 95 38 ff ff ff 	mov    %rdx,-0xc8(%rbp)
 c76:	8b 00                	mov    (%rax),%eax
 c78:	0f b6 c0             	movzbl %al,%eax
 c7b:	89 c7                	mov    %eax,%edi
 c7d:	e8 fc fa ff ff       	call   77e <print_byte_hex>
 c82:	e9 a6 00 00 00       	jmp    d2d <printf+0x496>
 c87:	8b 85 30 ff ff ff    	mov    -0xd0(%rbp),%eax
 c8d:	83 f8 2f             	cmp    $0x2f,%eax
 c90:	77 23                	ja     cb5 <printf+0x41e>
 c92:	48 8b 85 40 ff ff ff 	mov    -0xc0(%rbp),%rax
 c99:	8b 95 30 ff ff ff    	mov    -0xd0(%rbp),%edx
 c9f:	89 d2                	mov    %edx,%edx
 ca1:	48 01 d0             	add    %rdx,%rax
 ca4:	8b 95 30 ff ff ff    	mov    -0xd0(%rbp),%edx
 caa:	83 c2 08             	add    $0x8,%edx
 cad:	89 95 30 ff ff ff    	mov    %edx,-0xd0(%rbp)
 cb3:	eb 12                	jmp    cc7 <printf+0x430>
 cb5:	48 8b 85 38 ff ff ff 	mov    -0xc8(%rbp),%rax
 cbc:	48 8d 50 08          	lea    0x8(%rax),%rdx
 cc0:	48 89 95 38 ff ff ff 	mov    %rdx,-0xc8(%rbp)
 cc7:	8b 00                	mov    (%rax),%eax
 cc9:	0f b6 c0             	movzbl %al,%eax
 ccc:	89 c7                	mov    %eax,%edi
 cce:	e8 ab fa ff ff       	call   77e <print_byte_hex>
 cd3:	eb 58                	jmp    d2d <printf+0x496>
 cd5:	48 83 ad 48 ff ff ff 	subq   $0x1,-0xb8(%rbp)
 cdc:	01 
 cdd:	8b 15 41 33 00 00    	mov    0x3341(%rip),%edx        # 4024 <cursor+0x4>
 ce3:	8b 0d 37 33 00 00    	mov    0x3337(%rip),%ecx        # 4020 <cursor>
 ce9:	48 8b 85 48 ff ff ff 	mov    -0xb8(%rbp),%rax
 cf0:	0f b6 00             	movzbl (%rax),%eax
 cf3:	0f be c0             	movsbl %al,%eax
 cf6:	89 ce                	mov    %ecx,%esi
 cf8:	89 c7                	mov    %eax,%edi
 cfa:	e8 16 f7 ff ff       	call   415 <draw_char>
 cff:	e8 43 f8 ff ff       	call   547 <cursor_inc>
 d04:	eb 27                	jmp    d2d <printf+0x496>
 d06:	8b 15 18 33 00 00    	mov    0x3318(%rip),%edx        # 4024 <cursor+0x4>
 d0c:	8b 0d 0e 33 00 00    	mov    0x330e(%rip),%ecx        # 4020 <cursor>
 d12:	48 8b 85 48 ff ff ff 	mov    -0xb8(%rbp),%rax
 d19:	0f b6 00             	movzbl (%rax),%eax
 d1c:	0f be c0             	movsbl %al,%eax
 d1f:	89 ce                	mov    %ecx,%esi
 d21:	89 c7                	mov    %eax,%edi
 d23:	e8 ed f6 ff ff       	call   415 <draw_char>
 d28:	e8 1a f8 ff ff       	call   547 <cursor_inc>
 d2d:	48 83 85 48 ff ff ff 	addq   $0x1,-0xb8(%rbp)
 d34:	01 
 d35:	48 8b 85 48 ff ff ff 	mov    -0xb8(%rbp),%rax
 d3c:	0f b6 00             	movzbl (%rax),%eax
 d3f:	84 c0                	test   %al,%al
 d41:	0f 85 01 fc ff ff    	jne    948 <printf+0xb1>
 d47:	48 8b 85 28 ff ff ff 	mov    -0xd8(%rbp),%rax
 d4e:	c9                   	leave
 d4f:	c3                   	ret

0000000000000d50 <move_cursor>:
 d50:	55                   	push   %rbp
 d51:	48 89 e5             	mov    %rsp,%rbp
 d54:	48 83 ec 18          	sub    $0x18,%rsp
 d58:	89 7d ec             	mov    %edi,-0x14(%rbp)
 d5b:	8b 05 af 32 00 00    	mov    0x32af(%rip),%eax        # 4010 <p_scanln>
 d61:	c1 f8 03             	sar    $0x3,%eax
 d64:	89 45 f8             	mov    %eax,-0x8(%rbp)
 d67:	8b 05 b7 32 00 00    	mov    0x32b7(%rip),%eax        # 4024 <cursor+0x4>
 d6d:	0f af 45 f8          	imul   -0x8(%rbp),%eax
 d71:	89 c2                	mov    %eax,%edx
 d73:	8b 05 a7 32 00 00    	mov    0x32a7(%rip),%eax        # 4020 <cursor>
 d79:	01 d0                	add    %edx,%eax
 d7b:	89 45 fc             	mov    %eax,-0x4(%rbp)
 d7e:	8b 45 ec             	mov    -0x14(%rbp),%eax
 d81:	01 45 fc             	add    %eax,-0x4(%rbp)
 d84:	83 7d fc 00          	cmpl   $0x0,-0x4(%rbp)
 d88:	79 07                	jns    d91 <move_cursor+0x41>
 d8a:	c7 45 fc 00 00 00 00 	movl   $0x0,-0x4(%rbp)
 d91:	8b 45 fc             	mov    -0x4(%rbp),%eax
 d94:	99                   	cltd
 d95:	f7 7d f8             	idivl  -0x8(%rbp)
 d98:	89 05 86 32 00 00    	mov    %eax,0x3286(%rip)        # 4024 <cursor+0x4>
 d9e:	8b 45 fc             	mov    -0x4(%rbp),%eax
 da1:	99                   	cltd
 da2:	f7 7d f8             	idivl  -0x8(%rbp)
 da5:	89 d0                	mov    %edx,%eax
 da7:	89 05 73 32 00 00    	mov    %eax,0x3273(%rip)        # 4020 <cursor>
 dad:	90                   	nop
 dae:	c9                   	leave
 daf:	c3                   	ret

0000000000000db0 <cursor_Setpos>:
 db0:	55                   	push   %rbp
 db1:	48 89 e5             	mov    %rsp,%rbp
 db4:	48 83 ec 18          	sub    $0x18,%rsp
 db8:	89 7d ec             	mov    %edi,-0x14(%rbp)
 dbb:	89 75 e8             	mov    %esi,-0x18(%rbp)
 dbe:	8b 05 4c 32 00 00    	mov    0x324c(%rip),%eax        # 4010 <p_scanln>
 dc4:	c1 f8 03             	sar    $0x3,%eax
 dc7:	89 45 f8             	mov    %eax,-0x8(%rbp)
 dca:	8b 45 e8             	mov    -0x18(%rbp),%eax
 dcd:	0f af 45 f8          	imul   -0x8(%rbp),%eax
 dd1:	89 c2                	mov    %eax,%edx
 dd3:	8b 45 ec             	mov    -0x14(%rbp),%eax
 dd6:	01 d0                	add    %edx,%eax
 dd8:	89 45 fc             	mov    %eax,-0x4(%rbp)
 ddb:	83 7d fc 00          	cmpl   $0x0,-0x4(%rbp)
 ddf:	79 07                	jns    de8 <cursor_Setpos+0x38>
 de1:	c7 45 fc 00 00 00 00 	movl   $0x0,-0x4(%rbp)
 de8:	8b 45 fc             	mov    -0x4(%rbp),%eax
 deb:	99                   	cltd
 dec:	f7 7d f8             	idivl  -0x8(%rbp)
 def:	89 d0                	mov    %edx,%eax
 df1:	89 05 29 32 00 00    	mov    %eax,0x3229(%rip)        # 4020 <cursor>
 df7:	8b 45 fc             	mov    -0x4(%rbp),%eax
 dfa:	99                   	cltd
 dfb:	f7 7d f8             	idivl  -0x8(%rbp)
 dfe:	89 05 20 32 00 00    	mov    %eax,0x3220(%rip)        # 4024 <cursor+0x4>
 e04:	90                   	nop
 e05:	c9                   	leave
 e06:	c3                   	ret
