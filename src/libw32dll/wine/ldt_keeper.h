#ifndef LDT_KEEPER_H
#define LDT_KEEPER_H

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct {
  void* fs_seg;
  char* prev_struct;
  int fd;
} LDT_FS;

void Setup_FS_Segment(void);
LDT_FS * Setup_LDT_Keeper(void);
void Restore_LDT_Keeper(LDT_FS * ldt_fs);
#ifdef __cplusplus
}
#endif

#endif /* LDT_KEEPER_H */
