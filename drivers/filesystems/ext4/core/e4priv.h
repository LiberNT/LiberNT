/*
 * PROJECT:     LiberNT ext4 file system driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     What the files of the core share with each other
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#ifndef E4PRIV_H
#define E4PRIV_H

void E4Now(E4_VOLUME *Volume, E4_TIME *Time);
int E4Get(E4_VOLUME *Volume, uint32_t Inode, struct ext4_inode_ref *Ref);
int E4CountFree(E4_VOLUME *Volume);

void E4StageStart(E4_VOLUME *Volume);
void E4StageStop(E4_VOLUME *Volume);
int E4StageFlush(E4_VOLUME *Volume);
void E4StageCommit(E4_VOLUME *Volume);
int E4StageSettleFreed(E4_VOLUME *Volume);

#endif
