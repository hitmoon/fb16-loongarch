
#ifndef _LOONGARCH_KEXEC_H_
#define _LOONGARCH_KEXEC_H_

int
kexec_load_md(struct kexec_image *image)
{
  return (ENOSYS);
}

#define kexec_reboot_md(x) do {} while (0)
#endif /* _LOONGARCH_KEXEC_H_ */

