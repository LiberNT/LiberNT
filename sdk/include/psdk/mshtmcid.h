/*
 * Copyright 2006 Jacek Caban for CodeWeavers
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#ifndef __mshtmcid_h__
#define __mshtmcid_h__

#define IDM_UNKNOWN                 0
#define IDM_ALIGNBOTTOM             1
#define IDM_ALIGNHORIZONTALCENTERS  2
#define IDM_ALIGNLEFT               3
#define IDM_ALIGNRIGHT              4
#define IDM_ALIGNTOGRID             5
#define IDM_ALIGNTOP                6
#define IDM_ALIGNVERTICALCENTERS    7
#define IDM_ARRANGEBOTTOM           8
#define IDM_ARRANGERIGHT            9
#define IDM_BRINGFORWARD            10
#define IDM_BRINGTOFRONT            11
#define IDM_CENTERHORIZONTALLY      12
#define IDM_CENTERVERTICALLY        13
#define IDM_CODE                    14
#define IDM_COPY                    15
#define IDM_CUT                     16
#define IDM_DELETE                  17
#define IDM_FONTNAME                18
#define IDM_FONTSIZE                19
#define IDM_GROUP                   20
#define IDM_HORIZSPACECONCATENATE   21
#define IDM_HORIZSPACEDECREASE      22
#define IDM_HORIZSPACEINCREASE      23
#define IDM_HORIZSPACEMAKEEQUAL     24
#define IDM_INSERTOBJECT            25
#define IDM_PASTE                   26
#define IDM_PRINT                   27
#define IDM_PROPERTIES              28
#define IDM_REDO                    29
#define IDM_MULTILEVELREDO          30
#define IDM_SELECTALL               31
#define IDM_SENDBACKWARD            32
#define IDM_SENDTOBACK              33
#define IDM_SHOWTABLE               34
#define IDM_SIZETOCONTROL           35
#define IDM_SIZETOCONTROLHEIGHT     36
#define IDM_SIZETOCONTROLWIDTH      37
#define IDM_SIZETOFIT               38
#define IDM_SIZETOGRID              39
#define IDM_SNAPTOGRID              40
#define IDM_TABORDER                41
#define IDM_TOOLBOX                 42
#define IDM_UNDO                    43
#define IDM_MULTILEVELUNDO          44
#define IDM_UNGROUP                 45
#define IDM_VERTSPACECONCATENATE    46
#define IDM_VERTSPACEDECREASE       47
#define IDM_VERTSPACEINCREASE       48
#define IDM_VERTSPACEMAKEEQUAL      49
#define IDM_JUSTIFYFULL             50
#define IDM_ZOOMPERCENT             50
#define IDM_BACKCOLOR               51
#define IDM_BOLD                    52
#define IDM_BORDERCOLOR             53
#define IDM_FLAT                    54
#define IDM_FORECOLOR               55
#define IDM_ITALIC                  56
#define IDM_JUSTIFYCENTER           57
#define IDM_JUSTIFYGENERAL          58
#define IDM_JUSTIFYLEFT             59
#define IDM_JUSTIFYRIGHT            60
#define IDM_RAISED                  61
#define IDM_SUNKEN                  62
#define IDM_UNDERLINE               63
#define IDM_CHISELED                64
#define IDM_ETCHED                  65
#define IDM_SHADOWED                66
#define IDM_FIND                    67
#define IDM_GETZOOM                 68
#define IDM_SHOWGRID                69
#define IDM_SAVE                    70
#define IDM_SAVEAS                  71
#define IDM_OBJECTVERBLIST0         72
#define IDM_OBJECTVERBLIST1         73
#define IDM_OBJECTVERBLIST2         74
#define IDM_OBJECTVERBLIST3         75
#define IDM_OBJECTVERBLIST4         76
#define IDM_OBJECTVERBLIST5         77
#define IDM_OBJECTVERBLIST6         78
#define IDM_OBJECTVERBLIST7         79
#define IDM_OBJECTVERBLIST8         80
#define IDM_OBJECTVERBLIST9         81

#define IDM_OBJECTVERBLISTLAST      IDM_OBJECTVERBLIST9

#define IDM_CONVERTOBJECT           82
#define IDM_CUSTOMCONTROL           83
#define IDM_CUSTOMIZEITEM           84
#define IDM_RENAME                  85
#define IDM_IMPORT                  86
#define IDM_NEWPAGE                 87
#define IDM_MOVE                    88
#define IDM_CANCEL                  89
#define IDM_FONT                    90
#define IDM_STRIKETHROUGH           91
#define IDM_DELETEWORD              92
#define IDM_EXECPRINT               93
#define IDM_JUSTIFYNONE             94
#define IDM_TRISTATEBOLD            95
#define IDM_TRISTATEITALIC          96
#define IDM_TRISTATEUNDERLINE       97

#define IDM_OPEN                          2000
#define IDM_NEW                           2001
#define IDM_SAVECOPYAS                    2002
#define IDM_PRINTPREVIEW                  2003
#define IDM_PAGESETUP                     2004
#define IDM_SPELL                         2005
#define IDM_PASTESPECIAL                  2006
#define IDM_CLEARSELECTION                2007
#define IDM_FOLLOW_ANCHOR                 2008

#define IDM_SHOWPRINT                     2010
#define IDM_SHOWPAGESETUP                 2011

#define IDM_INSINPUTIMAGE                 2114
#define IDM_INSINPUTBUTTON                2115
#define IDM_INSINPUTRESET                 2116
#define IDM_INSINPUTSUBMIT                2117
#define IDM_INSINPUTUPLOAD                2118
#define IDM_INSFIELDSET                   2119
#define IDM_PASTEINSERT                   2120
#define IDM_REPLACE                       2121
#define IDM_EDITSOURCE                    2122
#define IDM_BOOKMARK                      2123
#define IDM_HYPERLINK                     2124
#define IDM_UNLINK                        2125
#define IDM_BROWSEMODE                    2126
#define IDM_EDITMODE                      2127
#define IDM_UNBOOKMARK                    2128

#define IDM_TOOLBARS                      2130
#define IDM_STATUSBAR                     2131
#define IDM_FORMATMARK                    2132
#define IDM_TEXTONLY                      2133

#define IDM_OPTIONS                       2135
#define IDM_FOLLOWLINKC                   2136
#define IDM_FOLLOWLINKN                   2137
#define IDM_STOP                          2138
#define IDM_VIEWSOURCE                    2139
#define IDM_ZOOMPOPUP                     2140
#define IDM_BASELINEFONT1                 2141
#define IDM_BASELINEFONT2                 2142
#define IDM_BASELINEFONT3                 2143
#define IDM_BASELINEFONT4                 2144
#define IDM_BASELINEFONT5                 2145

#define IDM_HORIZONTALLINE                2150
#define IDM_LINEBREAKNORMAL               2151
#define IDM_LINEBREAKLEFT                 2152
#define IDM_LINEBREAKRIGHT                2153
#define IDM_LINEBREAKBOTH                 2154
#define IDM_NONBREAK                      2155
#define IDM_SPECIALCHAR                   2156
#define IDM_HTMLSOURCE                    2157
#define IDM_IFRAME                        2158
#define IDM_HTMLCONTAIN                   2159

#define IDM_TEXTBOX                       2161
#define IDM_TEXTAREA                      2162
#define IDM_CHECKBOX                      2163
#define IDM_RADIOBUTTON                   2164
#define IDM_DROPDOWNBOX                   2165
#define IDM_LISTBOX                       2166
#define IDM_BUTTON                        2167
#define IDM_IMAGE                         2168
#define IDM_OBJECT                        2169
#define IDM_1D                            2170
#define IDM_IMAGEMAP                      2171
#define IDM_FILE                          2172
#define IDM_COMMENT                       2173
#define IDM_SCRIPT                        2174
#define IDM_JAVAAPPLET                    2175
#define IDM_PLUGIN                        2176
#define IDM_PAGEBREAK                     2177
#define IDM_HTMLAREA                      2178

#define IDM_PARAGRAPH                     2180
#define IDM_FORM                          2181
#define IDM_MARQUEE                       2182
#define IDM_LIST                          2183
#define IDM_ORDERLIST                     2184
#define IDM_UNORDERLIST                   2185
#define IDM_INDENT                        2186
#define IDM_OUTDENT                       2187
#define IDM_PREFORMATTED                  2188
#define IDM_ADDRESS                       2189
#define IDM_BLINK                         2190
#define IDM_DIV                           2191

#define IDM_TABLEINSERT                   2200
#define IDM_RCINSERT                      2201
#define IDM_CELLINSERT                    2202
#define IDM_CAPTIONINSERT                 2203
#define IDM_CELLMERGE                     2204
#define IDM_CELLSPLIT                     2205
#define IDM_CELLSELECT                    2206
#define IDM_ROWSELECT                     2207
#define IDM_COLUMNSELECT                  2208
#define IDM_TABLESELECT                   2209
#define IDM_TABLEPROPERTIES               2210
#define IDM_CELLPROPERTIES                2211
#define IDM_ROWINSERT                     2212
#define IDM_COLUMNINSERT                  2213

#define IDM_HELP_CONTENT                  2220
#define IDM_HELP_ABOUT                    2221
#define IDM_HELP_README                   2222

#define IDM_REMOVEFORMAT                  2230
#define IDM_PAGEINFO                      2231
#define IDM_TELETYPE                      2232
#define IDM_GETBLOCKFMTS                  2233
#define IDM_BLOCKFMT                      2234
#define IDM_SHOWHIDE_CODE                 2235
#define IDM_TABLE                         2236
#define IDM_COPYFORMAT                    2237
#define IDM_PASTEFORMAT                   2238
#define IDM_GOTO                          2239
#define IDM_CHANGEFONT                    2240
#define IDM_CHANGEFONTSIZE                2241

#define IDM_CHANGECASE                    2246
#define IDM_SUBSCRIPT                     2247
#define IDM_SUPERSCRIPT                   2248
#define IDM_SHOWSPECIALCHAR               2249
#define IDM_CENTERALIGNPARA               2250
#define IDM_LEFTALIGNPARA                 2251
#define IDM_RIGHTALIGNPARA                2252
#define IDM_REMOVEPARAFORMAT              2253
#define IDM_APPLYNORMAL                   2254
#define IDM_APPLYHEADING1                 2255
#define IDM_APPLYHEADING2                 2256
#define IDM_APPLYHEADING3                 2257

#define IDM_DOCPROPERTIES                 2260
#define IDM_ADDFAVORITES                  2261
#define IDM_COPYSHORTCUT                  2262
#define IDM_SAVEBACKGROUND                2263
#define IDM_SETWALLPAPER                  2264
#define IDM_COPYBACKGROUND                2265
#define IDM_CREATESHORTCUT                2266
#define IDM_PAGE                          2267
#define IDM_SAVETARGET                    2268
#define IDM_SHOWPICTURE                   2269
#define IDM_SAVEPICTURE                   2270
#define IDM_DYNSRCPLAY                    2271
#define IDM_DYNSRCSTOP                    2272
#define IDM_PRINTTARGET                   2273
#define IDM_IMGARTPLAY                    2274
#define IDM_IMGARTSTOP                    2275
#define IDM_IMGARTREWIND                  2276
#define IDM_PRINTQUERYJOBSPENDING         2277
#define IDM_SETDESKTOPITEM                2278

#define IDM_CONTEXTMENU                   2280

#define IDM_GOBACKWARD                    2282
#define IDM_GOFORWARD                     2283
#define IDM_PRESTOP                       2284

#define IDM_MP_MYPICS                     2287
#define IDM_MP_EMAILPICTURE               2288
#define IDM_MP_PRINTPICTURE               2289
#define IDM_CREATELINK                    2290
#define IDM_COPYCONTENT                   2291
#define IDM_LANGUAGE                      2292
 
#define IDM_GETPRINTTEMPLATE              2295
#define IDM_SETPRINTTEMPLATE              2296
#define IDM_TEMPLATE_PAGESETUP            2298

#define IDM_REFRESH                       2300
#define IDM_STOPDOWNLOAD                  2301
#define IDM_ENABLE_INTERACTION            2302

#define IDM_LAUNCHDEBUGGER                2310
#define IDM_BREAKATNEXT                   2311
#define IDM_INSINPUTHIDDEN                2312
#define IDM_INSINPUTPASSWORD              2313
#define IDM_OVERWRITE                     2314
#define IDM_PARSECOMPLETE                 2315
#define IDM_HTMLEDITMODE                  2316
#define IDM_REGISTRYREFRESH               2317
#define IDM_COMPOSESETTINGS               2318

#define IDM_SHOWMISCTAGS                  2320
#define IDM_SHOWALIGNEDSITETAGS           2321
#define IDM_SHOWSCRIPTTAGS                2322
#define IDM_SHOWSTYLETAGS                 2323
#define IDM_SHOWCOMMENTTAGS               2324
#define IDM_SHOWAREATAGS                  2325
#define IDM_SHOWUNKNOWNTAGS               2326
#define IDM_SHOWALLTAGS                   2327
#define IDM_SHOWZEROBORDERATDESIGNTIME    2328
#define IDM_AUTODETECT                    2329
#define IDM_SCRIPTDEBUGGER                2330
#define IDM_GETBYTESDOWNLOADED            2331
#define IDM_NOACTIVATENORMALOLECONTROLS   2332
#define IDM_NOACTIVATEDESIGNTIMECONTROLS  2333
#define IDM_NOACTIVATEJAVAAPPLETS         2334
#define IDM_NOFIXUPURLSONPASTE            2335
#define IDM_EMPTYGLYPHTABLE               2336
#define IDM_ADDTOGLYPHTABLE               2337
#define IDM_REMOVEFROMGLYPHTABLE          2338
#define IDM_REPLACEGLYPHCONTENTS          2339
#define IDM_SHOWWBRTAGS                   2340
#define IDM_PERSISTSTREAMSYNC             2341
#define IDM_SETDIRTY                      2342
#define IDM_RUNURLSCRIPT                  2343

#ifdef IE5_ZOOM
#define IDM_ZOOMRATIO                     2344
#define IDM_GETZOOMNUMERATOR              2345
#define IDM_GETZOOMDENOMINATOR            2346
#endif

#define IDM_DIRLTR                        2350
#define IDM_DIRRTL                        2351
#define IDM_BLOCKDIRLTR                   2352
#define IDM_BLOCKDIRRTL                   2353
#define IDM_INLINEDIRLTR                  2354
#define IDM_INLINEDIRRTL                  2355
#define IDM_ISTRUSTEDDLG                  2356
#define IDM_INSERTSPAN                    2357
#define IDM_LOCALIZEEDITOR                2358

#define IDM_SAVEPRETRANSFORMSOURCE        2370
#define IDM_VIEWPRETRANSFORMSOURCE        2371

#define IDM_SCROLL_HERE                   2380
#define IDM_SCROLL_TOP                    2381
#define IDM_SCROLL_BOTTOM                 2382
#define IDM_SCROLL_PAGEUP                 2383
#define IDM_SCROLL_PAGEDOWN               2384
#define IDM_SCROLL_UP                     2385
#define IDM_SCROLL_DOWN                   2386
#define IDM_SCROLL_LEFTEDGE               2387
#define IDM_SCROLL_RIGHTEDGE              2388
#define IDM_SCROLL_PAGELEFT               2389
#define IDM_SCROLL_PAGERIGHT              2390
#define IDM_SCROLL_LEFT                   2391
#define IDM_SCROLL_RIGHT                  2392
#define IDM_MULTIPLESELECTION             2393
#define IDM_2D_POSITION                   2394
#define IDM_2D_ELEMENT                    2395
#define IDM_1D_ELEMENT                    2396
#define IDM_ABSOLUTE_POSITION             2397
#define IDM_LIVERESIZE                    2398
#define IDM_ATOMICSELECTION               2399
#define IDM_AUTOURLDETECT_MODE            2400
#define IDM_IE50_PASTE                    2401
#define IDM_IE50_PASTE_MODE               2402
#define IDM_GETIPRINT                     2403
#define IDM_DISABLE_EDITFOCUS_UI          2404
#define IDM_RESPECTVISIBILITY_INDESIGN    2405
#define IDM_CSSEDITING_LEVEL              2406
#define IDM_UI_OUTDENT                    2407
#define IDM_UPDATEPAGESTATUS              2408
#define IDM_IME_ENABLE_RECONVERSION       2409
#define IDM_KEEPSELECTION                 2410
#define IDM_UNLOADDOCUMENT                2411

#define IDM_OVERRIDE_CURSOR               2420

#define IDM_PEERHITTESTSAMEINEDIT         2423

#define IDM_TRUSTAPPCACHE                 2425

#define IDM_BACKGROUNDIMAGECACHE          2430

#define IDM_MIMECSET__FIRST_  3609
#define IDM_MIMECSET__LAST__  3699

#define IDM_MENUEXT_FIRST__  3700
#define IDM_MENUEXT_LAST__   3732
#define IDM_MENUEXT_COUNT    3733

#define IDM_DEFAULTBLOCK  6046

#define IDM_NEW_TOPLEVELWINDOW  7050

#define IDM_PRESERVEUNDOALWAYS    6049
#define IDM_PERSISTDEFAULTVALUES  7100
#define IDM_PROTECTMETATAGS       7101

#define IDM_GETFRAMEZONE  6037

#define IDM_FIRE_PRINTTEMPLATEUP    15000
#define IDM_FIRE_PRINTTEMPLATEDOWN  15001
#define IDM_SETPRINTHANDLES         15002

#define CMD_ZOOM_PAGEWIDTH  -1
#define CMD_ZOOM_ONEPAGE    -2
#define CMD_ZOOM_TWOPAGES   -3
#define CMD_ZOOM_SELECTION  -4
#define CMD_ZOOM_FIT -5

#define IDM_CONTEXT  1
#define IDM_HWND     2

#ifndef IDM_ACTIVEXFILTERINGENABLED
#define IDM_ACTIVEXFILTERINGENABLED 15030
#endif
#ifndef IDM_ACTIVEXINSTALLSCOPE
#define IDM_ACTIVEXINSTALLSCOPE 15007
#endif
#ifndef IDM_ADDCONSOLEMESSAGERECEIVER
#define IDM_ADDCONSOLEMESSAGERECEIVER 3800
#endif
#ifndef IDM_ADDDEBUGCALLBACKRECEIVER
#define IDM_ADDDEBUGCALLBACKRECEIVER 3804
#endif
#ifndef IDM_ADDPARTIALTESTSTEPCOUNT
#define IDM_ADDPARTIALTESTSTEPCOUNT 15023
#endif
#ifndef IDM_ADDPDFHIGHLIGHT
#define IDM_ADDPDFHIGHLIGHT 15210
#endif
#ifndef IDM_ADDPDFNOTE
#define IDM_ADDPDFNOTE 15212
#endif
#ifndef IDM_BEGINUNDOUNIT
#define IDM_BEGINUNDOUNIT 3901
#endif
#ifndef IDM_BEGINUSERACTION
#define IDM_BEGINUSERACTION 2432
#endif
#ifndef IDM_BLUEHIGHLIGHT
#define IDM_BLUEHIGHLIGHT 15216
#endif
#ifndef IDM_CARETBROWSINGMODE
#define IDM_CARETBROWSINGMODE 2436
#endif
#ifndef IDM_CLEARAUTHENTICATIONCACHE
#define IDM_CLEARAUTHENTICATIONCACHE 15003
#endif
#ifndef IDM_CLEARUNDO
#define IDM_CLEARUNDO 3903
#endif
#ifndef IDM_DEBUGGERDYNAMICATTACH
#define IDM_DEBUGGERDYNAMICATTACH 15202
#endif
#ifndef IDM_DEBUGGERDYNAMICATTACHSOURCERUNDOWN
#define IDM_DEBUGGERDYNAMICATTACHSOURCERUNDOWN 15204
#endif
#ifndef IDM_DEBUGGERDYNAMICDETACH
#define IDM_DEBUGGERDYNAMICDETACH 15203
#endif
#ifndef IDM_DEFAULTPARAGRAPHSEPARATOR
#define IDM_DEFAULTPARAGRAPHSEPARATOR 3900
#endif
#ifndef IDM_DELETEPDFHIGHLIGHT
#define IDM_DELETEPDFHIGHLIGHT 15211
#endif
#ifndef IDM_EDITPDFHIGHLIGHT
#define IDM_EDITPDFHIGHLIGHT 15214
#endif
#ifndef IDM_ENABLEFLIPAHEADTARGET
#define IDM_ENABLEFLIPAHEADTARGET 15201
#endif
#ifndef IDM_ENABLE_OBJECT_RESIZING
#define IDM_ENABLE_OBJECT_RESIZING 3906
#endif
#ifndef IDM_ENDUNDOUNIT
#define IDM_ENDUNDOUNIT 3902
#endif
#ifndef IDM_ENDUSERACTION
#define IDM_ENDUSERACTION 2433
#endif
#ifndef IDM_FOLLOWLINKEDGE
#define IDM_FOLLOWLINKEDGE 3911
#endif
#ifndef IDM_FOLLOWLINKN_INPRIVATE
#define IDM_FOLLOWLINKN_INPRIVATE 3909
#endif
#ifndef IDM_FOLLOWLINKT
#define IDM_FOLLOWLINKT 2435
#endif
#ifndef IDM_FOLLOWLINKT_INPRIVATE
#define IDM_FOLLOWLINKT_INPRIVATE 3910
#endif
#ifndef IDM_FORWARDDELETE
#define IDM_FORWARDDELETE 98
#endif
#ifndef IDM_GETDEBUGGERSTATE
#define IDM_GETDEBUGGERSTATE 15205
#endif
#ifndef IDM_GETDEFAULTBACKGROUNDCOLOR
#define IDM_GETDEFAULTBACKGROUNDCOLOR 15044
#endif
#ifndef IDM_GETDEFAULTZOOMLEVEL
#define IDM_GETDEFAULTZOOMLEVEL 15027
#endif
#ifndef IDM_GETDOCDLGFLAGS
#define IDM_GETDOCDLGFLAGS 15005
#endif
#ifndef IDM_GETELEMENTBOUNDINGBOX
#define IDM_GETELEMENTBOUNDINGBOX 15028
#endif
#ifndef IDM_GETL9QUIRKSEMULATIONENABLED
#define IDM_GETL9QUIRKSEMULATIONENABLED 15025
#endif
#ifndef IDM_GETPARTIALLAYOUTSTATUS
#define IDM_GETPARTIALLAYOUTSTATUS 15022
#endif
#ifndef IDM_GETPRINTMANAGERDOCSOURCE
#define IDM_GETPRINTMANAGERDOCSOURCE 15038
#endif
#ifndef IDM_GETPRINTMANAGERDOCSOURCEASYNC
#define IDM_GETPRINTMANAGERDOCSOURCEASYNC 15047
#endif
#ifndef IDM_GETPROFILINGONSTART
#define IDM_GETPROFILINGONSTART 15011
#endif
#ifndef IDM_GETSCRIPTENGINE
#define IDM_GETSCRIPTENGINE 3803
#endif
#ifndef IDM_GETSESSIONDOCUMENTMODE
#define IDM_GETSESSIONDOCUMENTMODE 15009
#endif
#ifndef IDM_GETUSERACTIONTIME
#define IDM_GETUSERACTIONTIME 2431
#endif
#ifndef IDM_GETUSERINITFLAGS
#define IDM_GETUSERINITFLAGS 15004
#endif
#ifndef IDM_GOTOCLIPBOARDADDRESS
#define IDM_GOTOCLIPBOARDADDRESS 2285
#endif
#ifndef IDM_GOTOCLIPBOARDTEXT
#define IDM_GOTOCLIPBOARDTEXT 2286
#endif
#ifndef IDM_GREENHIGHLIGHT
#define IDM_GREENHIGHLIGHT 15217
#endif
#ifndef IDM_INSERTHTML
#define IDM_INSERTHTML 2502
#endif
#ifndef IDM_INSERTTEXT
#define IDM_INSERTTEXT 3907
#endif
#ifndef IDM_INSPECTELEMENT
#define IDM_INSPECTELEMENT 3904
#endif
#ifndef IDM_INVOKEFLIPAHEADTARGET
#define IDM_INVOKEFLIPAHEADTARGET 15200
#endif
#ifndef IDM_LAUNCHURICALLBACK
#define IDM_LAUNCHURICALLBACK 3908
#endif
#ifndef IDM_MEDIA_FRAMESTEP_BACK
#define IDM_MEDIA_FRAMESTEP_BACK 2461
#endif
#ifndef IDM_MEDIA_FRAMESTEP_FWD
#define IDM_MEDIA_FRAMESTEP_FWD 2460
#endif
#ifndef IDM_MEDIA_FULLSCREEN_EXIT
#define IDM_MEDIA_FULLSCREEN_EXIT 2447
#endif
#ifndef IDM_MEDIA_FULLSCREEN_TOGGLE
#define IDM_MEDIA_FULLSCREEN_TOGGLE 2446
#endif
#ifndef IDM_MEDIA_MUTE
#define IDM_MEDIA_MUTE 2462
#endif
#ifndef IDM_MEDIA_MUTEUNMUTE
#define IDM_MEDIA_MUTEUNMUTE 2442
#endif
#ifndef IDM_MEDIA_PAUSE
#define IDM_MEDIA_PAUSE 2444
#endif
#ifndef IDM_MEDIA_PLAY
#define IDM_MEDIA_PLAY 2443
#endif
#ifndef IDM_MEDIA_PLAYPAUSE
#define IDM_MEDIA_PLAYPAUSE 2441
#endif
#ifndef IDM_MEDIA_PLAYRATE0
#define IDM_MEDIA_PLAYRATE0 2480
#endif
#ifndef IDM_MEDIA_PLAYRATE1
#define IDM_MEDIA_PLAYRATE1 2481
#endif
#ifndef IDM_MEDIA_PLAYRATE2
#define IDM_MEDIA_PLAYRATE2 2482
#endif
#ifndef IDM_MEDIA_PLAYRATE3
#define IDM_MEDIA_PLAYRATE3 2483
#endif
#ifndef IDM_MEDIA_PLAYRATE4
#define IDM_MEDIA_PLAYRATE4 2484
#endif
#ifndef IDM_MEDIA_PLAYRATE5
#define IDM_MEDIA_PLAYRATE5 2485
#endif
#ifndef IDM_MEDIA_PLAYRATE6
#define IDM_MEDIA_PLAYRATE6 2486
#endif
#ifndef IDM_MEDIA_PLAYRATE7
#define IDM_MEDIA_PLAYRATE7 2487
#endif
#ifndef IDM_MEDIA_PLAYRATE8
#define IDM_MEDIA_PLAYRATE8 2488
#endif
#ifndef IDM_MEDIA_PLAYRATE9
#define IDM_MEDIA_PLAYRATE9 2489
#endif
#ifndef IDM_MEDIA_RATE_FASTER
#define IDM_MEDIA_RATE_FASTER 2456
#endif
#ifndef IDM_MEDIA_RATE_SLOWER
#define IDM_MEDIA_RATE_SLOWER 2457
#endif
#ifndef IDM_MEDIA_SEEK_BACK_LARGE
#define IDM_MEDIA_SEEK_BACK_LARGE 2455
#endif
#ifndef IDM_MEDIA_SEEK_BACK_SMALL
#define IDM_MEDIA_SEEK_BACK_SMALL 2453
#endif
#ifndef IDM_MEDIA_SEEK_FWD_LARGE
#define IDM_MEDIA_SEEK_FWD_LARGE 2454
#endif
#ifndef IDM_MEDIA_SEEK_FWD_SMALL
#define IDM_MEDIA_SEEK_FWD_SMALL 2452
#endif
#ifndef IDM_MEDIA_SEEK_TO_END
#define IDM_MEDIA_SEEK_TO_END 2451
#endif
#ifndef IDM_MEDIA_SEEK_TO_START
#define IDM_MEDIA_SEEK_TO_START 2450
#endif
#ifndef IDM_MEDIA_SHOWCONTROLS_TOGGLE
#define IDM_MEDIA_SHOWCONTROLS_TOGGLE 2458
#endif
#ifndef IDM_MEDIA_SHOW_AUDIO_ACCESS
#define IDM_MEDIA_SHOW_AUDIO_ACCESS 2464
#endif
#ifndef IDM_MEDIA_SHOW_SUBTITLE_ACCESS
#define IDM_MEDIA_SHOW_SUBTITLE_ACCESS 2465
#endif
#ifndef IDM_MEDIA_STOP
#define IDM_MEDIA_STOP 2445
#endif
#ifndef IDM_MEDIA_UNMUTE
#define IDM_MEDIA_UNMUTE 2463
#endif
#ifndef IDM_MEDIA_VOLUME_DOWN
#define IDM_MEDIA_VOLUME_DOWN 2449
#endif
#ifndef IDM_MEDIA_VOLUME_UP
#define IDM_MEDIA_VOLUME_UP 2448
#endif
#ifndef IDM_MEDIA_ZOOMMODE_TOGGLE
#define IDM_MEDIA_ZOOMMODE_TOGGLE 2459
#endif
#ifndef IDM_MENUEXT_PLACEHOLDER
#define IDM_MENUEXT_PLACEHOLDER 6047
#endif
#ifndef IDM_MIMECSET__FIRST__
#define IDM_MIMECSET__FIRST__ 3609
#endif
#ifndef IDM_NONEHIGHLIGHT
#define IDM_NONEHIGHLIGHT 15219
#endif
#ifndef IDM_NOTIFYCONTEXTMENUDISMISSED
#define IDM_NOTIFYCONTEXTMENUDISMISSED 15046
#endif
#ifndef IDM_NOTIFYZOOMANDSCROLLANIMATIONEND
#define IDM_NOTIFYZOOMANDSCROLLANIMATIONEND 15045
#endif
#ifndef IDM_OLEWINDOWSTATECHANGED
#define IDM_OLEWINDOWSTATECHANGED 15006
#endif
#ifndef IDM_OPENPDFNOTE
#define IDM_OPENPDFNOTE 15213
#endif
#ifndef IDM_PASTECONTENTONLY
#define IDM_PASTECONTENTONLY 2500
#endif
#ifndef IDM_PASTETEXTONLY
#define IDM_PASTETEXTONLY 2501
#endif
#ifndef IDM_PDFDEFINE
#define IDM_PDFDEFINE 15222
#endif
#ifndef IDM_PDFREADALOUD
#define IDM_PDFREADALOUD 15220
#endif
#ifndef IDM_PERFORMEDITACTIVATION
#define IDM_PERFORMEDITACTIVATION 15042
#endif
#ifndef IDM_PINKHIGHLIGHT
#define IDM_PINKHIGHLIGHT 15215
#endif
#ifndef IDM_POPSTATEEVENT
#define IDM_POPSTATEEVENT 15017
#endif
#ifndef IDM_REFRESH_THIS
#define IDM_REFRESH_THIS 6042
#endif
#ifndef IDM_REMOVECONSOLEMESSAGERECEIVER
#define IDM_REMOVECONSOLEMESSAGERECEIVER 3801
#endif
#ifndef IDM_REMOVEDEBUGCALLBACKRECEIVER
#define IDM_REMOVEDEBUGCALLBACKRECEIVER 3805
#endif
#ifndef IDM_RUNFLASH
#define IDM_RUNFLASH 15208
#endif
#ifndef IDM_SAVEPDF
#define IDM_SAVEPDF 99
#endif
#ifndef IDM_SELECTIONSEARCH
#define IDM_SELECTIONSEARCH 15206
#endif
#ifndef IDM_SETACCESSIBILITYNAME
#define IDM_SETACCESSIBILITYNAME 15040
#endif
#ifndef IDM_SETCUSTOMCURSOR
#define IDM_SETCUSTOMCURSOR 2434
#endif
#ifndef IDM_SETDEFAULTBACKGROUNDCOLOR
#define IDM_SETDEFAULTBACKGROUNDCOLOR 15043
#endif
#ifndef IDM_SETDEVTOOLBARCONSOLE
#define IDM_SETDEVTOOLBARCONSOLE 15016
#endif
#ifndef IDM_SETEXTRAHEADERS
#define IDM_SETEXTRAHEADERS 15039
#endif
#ifndef IDM_SETGEOLOCATIONCONSENT
#define IDM_SETGEOLOCATIONCONSENT 15029
#endif
#ifndef IDM_SETL9QUIRKSEMULATIONENABLED
#define IDM_SETL9QUIRKSEMULATIONENABLED 15024
#endif
#ifndef IDM_SETNAVIGATEEVENTSINK
#define IDM_SETNAVIGATEEVENTSINK 15013
#endif
#ifndef IDM_SETPAGEACTIONALLOWEDFLAGS
#define IDM_SETPAGEACTIONALLOWEDFLAGS 15100
#endif
#ifndef IDM_SETPARTIALLAYOUTSTATUS
#define IDM_SETPARTIALLAYOUTSTATUS 15021
#endif
#ifndef IDM_SETPOINTERLOCKCONSENT
#define IDM_SETPOINTERLOCKCONSENT 15026
#endif
#ifndef IDM_SETPROFILINGONSTART
#define IDM_SETPROFILINGONSTART 15010
#endif
#ifndef IDM_SETSCRIPTCONSOLE
#define IDM_SETSCRIPTCONSOLE 15012
#endif
#ifndef IDM_SETSESSIONDOCUMENTMODE
#define IDM_SETSESSIONDOCUMENTMODE 15008
#endif
#ifndef IDM_SHARE
#define IDM_SHARE 15031
#endif
#ifndef IDM_SHAREAPPCACHEEVENT
#define IDM_SHAREAPPCACHEEVENT 15033
#endif
#ifndef IDM_SHAREPDF
#define IDM_SHAREPDF 15221
#endif
#ifndef IDM_SHAREPICTURE
#define IDM_SHAREPICTURE 3905
#endif
#ifndef IDM_SHOWSHAREUI
#define IDM_SHOWSHAREUI 15207
#endif
#ifndef IDM_STARTDIAGNOSTICSMODE
#define IDM_STARTDIAGNOSTICSMODE 3802
#endif
#ifndef IDM_STYLEMENU_CHANGESELECTEDSTYLE
#define IDM_STYLEMENU_CHANGESELECTEDSTYLE 2440
#endif
#ifndef IDM_STYLEMENU_GETNOSTYLE
#define IDM_STYLEMENU_GETNOSTYLE 2438
#endif
#ifndef IDM_STYLEMENU_GETPREFSTYLE
#define IDM_STYLEMENU_GETPREFSTYLE 2439
#endif
#ifndef IDM_STYLEMENU_SETNOSTYLE
#define IDM_STYLEMENU_SETNOSTYLE 2437
#endif
#ifndef IDM_TOGGLEREADINGBAR
#define IDM_TOGGLEREADINGBAR 15209
#endif
#ifndef IDM_UPDATESETTINGSFROMREGISTRY
#define IDM_UPDATESETTINGSFROMREGISTRY 15041
#endif
#ifndef IDM_YELLOWHIGHLIGHT
#define IDM_YELLOWHIGHLIGHT 15218
#endif

#endif
