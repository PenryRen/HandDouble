import re, pathlib
source=pathlib.Path('src/usb_hid.c').read_text()
names=sorted(set(re.findall(r'\b(?:RB_|MASK_|UIS_|UEP_)[A-Z0-9_]+',source)))
sfr=pathlib.Path('StdPeriphDriver/inc/CH583SFR.h').read_text(errors='replace')
lines=['#ifndef MOCK_CH58X_H','#define MOCK_CH58X_H','#include <stdint.h>',
       '#define __INTERRUPT','#define __HIGH_CODE','#define USB_IRQn 0',
       '#define PFIC_EnableIRQ(x) ((void)(x))','#define PFIC_DisableIRQ(x) ((void)(x))']
regs=sorted(set(re.findall(r'\bR(?:8|16)_[A-Z0-9_]+',source)))
declarations=['volatile uint'+('16' if r.startswith('R16') else '8')+'_t '+r+';' for r in regs]
lines += ['extern '+d for d in declarations]
for n in names:
    m=re.search(r'^#define\s+'+n+r'\s+([^\r\n/]+)',sfr,re.M)
    assert m,n
    lines.append('#define '+n+' '+m.group(1).strip())
lines+=['#endif']
pathlib.Path('tests/mock/CH58x_common.h').write_text('\n'.join(lines)+'\n')
pathlib.Path('tests/mock/registers.c').write_text('#include "CH58x_common.h"\n'+'\n'.join(declarations)+'\n')
