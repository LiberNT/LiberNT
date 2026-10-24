
#if defined(_M_IX86) || defined(_M_AMD64)
#include <asm.inc>
#elif defined(_M_ARM)
#include <kxarm.h>
#endif

#ifdef _M_IX86
.code32

THUNK_ENTRY MACRO num:REQ
    ALIGN 4
    PUBLIC _ObjectStublessClient&num
    _ObjectStublessClient&num:
    mov eax, num
    jmp _call_stubless_func
ENDM

THUNK_ENTRY_VTBL MACRO num:REQ
    ALIGN 4
    PUBLIC _NdrProxyForwardingFunction&num
    _NdrProxyForwardingFunction&num:
    mov eax, [esp + 4]
    mov eax, [eax + 16]
    mov [esp + 4], eax
    mov eax, [eax]
    DB 0ffh, 0a0h /* jmp *offset(%eax) */
    DD 4 * num
ENDM

#elif defined(_M_AMD64)
.code64

THUNK_ENTRY MACRO num:REQ
    ALIGN 4
    PUBLIC ObjectStublessClient&num
    ObjectStublessClient&num:
    mov r10d, num
    jmp call_stubless_func
ENDM

THUNK_ENTRY_VTBL MACRO num:REQ
    ALIGN 4
    PUBLIC NdrProxyForwardingFunction&num
    NdrProxyForwardingFunction&num:
    mov rcx, [rcx + 020h]
    mov rax, [rcx]
    DB 0ffh, 0a0h /* jmp *offset(%rax) */
    DD 8 * num
ENDM

#elif defined(_M_ARM)
.code32

// FIXME: this is probably broken
THUNK_ENTRY MACRO num:REQ
    ldr ip, .number
    b.w call_stubless_func
    .number DB num
ENDM

THUNK_ENTRY_VTBL MACRO num:REQ
    ALIGN 4
    ldr r0, [r0, 10h]
    ldr ip, [r0]
    ldr pc, [ip, 4 * num]
ENDM

#elif defined(_M_ARM64)

    .macro THUNK_ENTRY num
    .global ObjectStublessClient\num
ObjectStublessClient\num:
    mov w16, #\num
    b call_stubless_func
    .endm

    .macro THUNK_ENTRY_VTBL num
    .global NdrProxyForwardingFunction\num
NdrProxyForwardingFunction\num:
    ldr x0, [x0, #0x20]
    ldr x16, [x0]
    mov x17, #\num
    ldr x16, [x16, x17, lsl #3]
    br x16
    .endm

#define ALL_THUNK_ENTRIES \
    T(3) T(4) T(5) T(6) T(7) T(8) T(9) T(10) T(11) T(12) T(13) T(14) T(15) \
    T(16) T(17) T(18) T(19) T(20) T(21) T(22) T(23) T(24) T(25) T(26) T(27) T(28) T(29) T(30) T(31) \
    T(32) T(33) T(34) T(35) T(36) T(37) T(38) T(39) T(40) T(41) T(42) T(43) T(44) T(45) T(46) T(47) \
    T(48) T(49) T(50) T(51) T(52) T(53) T(54) T(55) T(56) T(57) T(58) T(59) T(60) T(61) T(62) T(63) \
    T(64) T(65) T(66) T(67) T(68) T(69) T(70) T(71) T(72) T(73) T(74) T(75) T(76) T(77) T(78) T(79) \
    T(80) T(81) T(82) T(83) T(84) T(85) T(86) T(87) T(88) T(89) T(90) T(91) T(92) T(93) T(94) T(95) \
    T(96) T(97) T(98) T(99) T(100) T(101) T(102) T(103) T(104) T(105) T(106) T(107) T(108) T(109) T(110) T(111) \
    T(112) T(113) T(114) T(115) T(116) T(117) T(118) T(119) T(120) T(121) T(122) T(123) T(124) T(125) T(126) T(127) \
    T(128) T(129) T(130) T(131) T(132) T(133) T(134) T(135) T(136) T(137) T(138) T(139) T(140) T(141) T(142) T(143) \
    T(144) T(145) T(146) T(147) T(148) T(149) T(150) T(151) T(152) T(153) T(154) T(155) T(156) T(157) T(158) T(159) \
    T(160) T(161) T(162) T(163) T(164) T(165) T(166) T(167) T(168) T(169) T(170) T(171) T(172) T(173) T(174) T(175) \
    T(176) T(177) T(178) T(179) T(180) T(181) T(182) T(183) T(184) T(185) T(186) T(187) T(188) T(189) T(190) T(191) \
    T(192) T(193) T(194) T(195) T(196) T(197) T(198) T(199) T(200) T(201) T(202) T(203) T(204) T(205) T(206) T(207) \
    T(208) T(209) T(210) T(211) T(212) T(213) T(214) T(215) T(216) T(217) T(218) T(219) T(220) T(221) T(222) T(223) \
    T(224) T(225) T(226) T(227) T(228) T(229) T(230) T(231) T(232) T(233) T(234) T(235) T(236) T(237) T(238) T(239) \
    T(240) T(241) T(242) T(243) T(244) T(245) T(246) T(247) T(248) T(249) T(250) T(251) T(252) T(253) T(254) T(255) \
    T(256) T(257) T(258) T(259) T(260) T(261) T(262) T(263) T(264) T(265) T(266) T(267) T(268) T(269) T(270) T(271) \
    T(272) T(273) T(274) T(275) T(276) T(277) T(278) T(279) T(280) T(281) T(282) T(283) T(284) T(285) T(286) T(287) \
    T(288) T(289) T(290) T(291) T(292) T(293) T(294) T(295) T(296) T(297) T(298) T(299) T(300) T(301) T(302) T(303) \
    T(304) T(305) T(306) T(307) T(308) T(309) T(310) T(311) T(312) T(313) T(314) T(315) T(316) T(317) T(318) T(319) \
    T(320) T(321) T(322) T(323) T(324) T(325) T(326) T(327) T(328) T(329) T(330) T(331) T(332) T(333) T(334) T(335) \
    T(336) T(337) T(338) T(339) T(340) T(341) T(342) T(343) T(344) T(345) T(346) T(347) T(348) T(349) T(350) T(351) \
    T(352) T(353) T(354) T(355) T(356) T(357) T(358) T(359) T(360) T(361) T(362) T(363) T(364) T(365) T(366) T(367) \
    T(368) T(369) T(370) T(371) T(372) T(373) T(374) T(375) T(376) T(377) T(378) T(379) T(380) T(381) T(382) T(383) \
    T(384) T(385) T(386) T(387) T(388) T(389) T(390) T(391) T(392) T(393) T(394) T(395) T(396) T(397) T(398) T(399) \
    T(400) T(401) T(402) T(403) T(404) T(405) T(406) T(407) T(408) T(409) T(410) T(411) T(412) T(413) T(414) T(415) \
    T(416) T(417) T(418) T(419) T(420) T(421) T(422) T(423) T(424) T(425) T(426) T(427) T(428) T(429) T(430) T(431) \
    T(432) T(433) T(434) T(435) T(436) T(437) T(438) T(439) T(440) T(441) T(442) T(443) T(444) T(445) T(446) T(447) \
    T(448) T(449) T(450) T(451) T(452) T(453) T(454) T(455) T(456) T(457) T(458) T(459) T(460) T(461) T(462) T(463) \
    T(464) T(465) T(466) T(467) T(468) T(469) T(470) T(471) T(472) T(473) T(474) T(475) T(476) T(477) T(478) T(479) \
    T(480) T(481) T(482) T(483) T(484) T(485) T(486) T(487) T(488) T(489) T(490) T(491) T(492) T(493) T(494) T(495) \
    T(496) T(497) T(498) T(499) T(500) T(501) T(502) T(503) T(504) T(505) T(506) T(507) T(508) T(509) T(510) T(511) \
    T(512) T(513) T(514) T(515) T(516) T(517) T(518) T(519) T(520) T(521) T(522) T(523) T(524) T(525) T(526) T(527) \
    T(528) T(529) T(530) T(531) T(532) T(533) T(534) T(535) T(536) T(537) T(538) T(539) T(540) T(541) T(542) T(543) \
    T(544) T(545) T(546) T(547) T(548) T(549) T(550) T(551) T(552) T(553) T(554) T(555) T(556) T(557) T(558) T(559) \
    T(560) T(561) T(562) T(563) T(564) T(565) T(566) T(567) T(568) T(569) T(570) T(571) T(572) T(573) T(574) T(575) \
    T(576) T(577) T(578) T(579) T(580) T(581) T(582) T(583) T(584) T(585) T(586) T(587) T(588) T(589) T(590) T(591) \
    T(592) T(593) T(594) T(595) T(596) T(597) T(598) T(599) T(600) T(601) T(602) T(603) T(604) T(605) T(606) T(607) \
    T(608) T(609) T(610) T(611) T(612) T(613) T(614) T(615) T(616) T(617) T(618) T(619) T(620) T(621) T(622) T(623) \
    T(624) T(625) T(626) T(627) T(628) T(629) T(630) T(631) T(632) T(633) T(634) T(635) T(636) T(637) T(638) T(639) \
    T(640) T(641) T(642) T(643) T(644) T(645) T(646) T(647) T(648) T(649) T(650) T(651) T(652) T(653) T(654) T(655) \
    T(656) T(657) T(658) T(659) T(660) T(661) T(662) T(663) T(664) T(665) T(666) T(667) T(668) T(669) T(670) T(671) \
    T(672) T(673) T(674) T(675) T(676) T(677) T(678) T(679) T(680) T(681) T(682) T(683) T(684) T(685) T(686) T(687) \
    T(688) T(689) T(690) T(691) T(692) T(693) T(694) T(695) T(696) T(697) T(698) T(699) T(700) T(701) T(702) T(703) \
    T(704) T(705) T(706) T(707) T(708) T(709) T(710) T(711) T(712) T(713) T(714) T(715) T(716) T(717) T(718) T(719) \
    T(720) T(721) T(722) T(723) T(724) T(725) T(726) T(727) T(728) T(729) T(730) T(731) T(732) T(733) T(734) T(735) \
    T(736) T(737) T(738) T(739) T(740) T(741) T(742) T(743) T(744) T(745) T(746) T(747) T(748) T(749) T(750) T(751) \
    T(752) T(753) T(754) T(755) T(756) T(757) T(758) T(759) T(760) T(761) T(762) T(763) T(764) T(765) T(766) T(767) \
    T(768) T(769) T(770) T(771) T(772) T(773) T(774) T(775) T(776) T(777) T(778) T(779) T(780) T(781) T(782) T(783) \
    T(784) T(785) T(786) T(787) T(788) T(789) T(790) T(791) T(792) T(793) T(794) T(795) T(796) T(797) T(798) T(799) \
    T(800) T(801) T(802) T(803) T(804) T(805) T(806) T(807) T(808) T(809) T(810) T(811) T(812) T(813) T(814) T(815) \
    T(816) T(817) T(818) T(819) T(820) T(821) T(822) T(823) T(824) T(825) T(826) T(827) T(828) T(829) T(830) T(831) \
    T(832) T(833) T(834) T(835) T(836) T(837) T(838) T(839) T(840) T(841) T(842) T(843) T(844) T(845) T(846) T(847) \
    T(848) T(849) T(850) T(851) T(852) T(853) T(854) T(855) T(856) T(857) T(858) T(859) T(860) T(861) T(862) T(863) \
    T(864) T(865) T(866) T(867) T(868) T(869) T(870) T(871) T(872) T(873) T(874) T(875) T(876) T(877) T(878) T(879) \
    T(880) T(881) T(882) T(883) T(884) T(885) T(886) T(887) T(888) T(889) T(890) T(891) T(892) T(893) T(894) T(895) \
    T(896) T(897) T(898) T(899) T(900) T(901) T(902) T(903) T(904) T(905) T(906) T(907) T(908) T(909) T(910) T(911) \
    T(912) T(913) T(914) T(915) T(916) T(917) T(918) T(919) T(920) T(921) T(922) T(923) T(924) T(925) T(926) T(927) \
    T(928) T(929) T(930) T(931) T(932) T(933) T(934) T(935) T(936) T(937) T(938) T(939) T(940) T(941) T(942) T(943) \
    T(944) T(945) T(946) T(947) T(948) T(949) T(950) T(951) T(952) T(953) T(954) T(955) T(956) T(957) T(958) T(959) \
    T(960) T(961) T(962) T(963) T(964) T(965) T(966) T(967) T(968) T(969) T(970) T(971) T(972) T(973) T(974) T(975) \
    T(976) T(977) T(978) T(979) T(980) T(981) T(982) T(983) T(984) T(985) T(986) T(987) T(988) T(989) T(990) T(991) \
    T(992) T(993) T(994) T(995) T(996) T(997) T(998) T(999) T(1000) T(1001) T(1002) T(1003) T(1004) T(1005) T(1006) T(1007) \
    T(1008) T(1009) T(1010) T(1011) T(1012) T(1013) T(1014) T(1015) T(1016) T(1017) T(1018) T(1019) T(1020) T(1021) T(1022) T(1023)

    .text

    .align 4
    .seh_proc stubless_thunks
    .global stubless_thunks
stubless_thunks:
#define T(num) THUNK_ENTRY num;
    ALL_THUNK_ENTRIES
#undef T
    .seh_endproc

    .align 4
    .seh_proc vtbl_thunks
    .global vtbl_thunks
vtbl_thunks:
#define T(num) THUNK_ENTRY_VTBL num;
    ALL_THUNK_ENTRIES
#undef T
    .seh_endproc

#endif

#ifndef _M_ARM64
ALL_THUNK_ENTRIES MACRO entry
    blk = 3
    REPT 1021
        entry %blk
        blk = blk + 1
    ENDM
ENDM

#ifdef _M_IX86
EXTERN _call_stubless_func:PROC
PUBLIC _stubless_thunks
_stubless_thunks:
    ALL_THUNK_ENTRIES THUNK_ENTRY
PUBLIC _vtbl_thunks
_vtbl_thunks:
    ALL_THUNK_ENTRIES THUNK_ENTRY_VTBL
#else
EXTERN call_stubless_func:PROC
PUBLIC stubless_thunks
stubless_thunks:
    ALL_THUNK_ENTRIES THUNK_ENTRY
PUBLIC vtbl_thunks
vtbl_thunks:
    ALL_THUNK_ENTRIES THUNK_ENTRY_VTBL
#endif

END
#endif
