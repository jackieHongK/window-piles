# Grades Poly Haven "Black Walnut Veneer 02" (CC0) to the oil finish used everywhere, and cuts the page crops.
# usage: python grade_walnut.py <dir with black_walnut_veneer_02_{diff,nor_gl,rough}_4k.jpg> <tex_out_dir> <page_out_dir>
import sys
import numpy as np
from PIL import Image

src, tex_out, page_out = sys.argv[1], sys.argv[2], sys.argv[3]
name = lambda m: f'{src}/black_walnut_veneer_02_{m}_4k.jpg'
d = np.asarray(Image.open(name('diff')).convert('RGB')).astype(np.float32) / 255
lin = d ** 2.2
mean = lin.mean(axis=(0, 1))
oiled = (lin / mean) ** 1.35 * mean * np.array([0.56, 0.44, 0.36])   # oil deepens the late-wood lines; warm chocolate
out = np.clip(oiled, 0, 1) ** (1 / 2.2)
rough = np.asarray(Image.open(name('rough')).convert('L')).astype(np.float32) / 255
rough = 0.32 + rough * 0.3                                              # satin: pores stay a bit rougher than the flats
Image.fromarray((out * 255 + 0.5).astype(np.uint8)).save(f'{tex_out}/walnut_diff_4k.jpg', quality=92)
Image.fromarray((rough * 255 + 0.5).astype(np.uint8)).save(f'{tex_out}/walnut_rough_4k.jpg', quality=92)
Image.open(name('nor_gl')).convert('RGB').save(f'{tex_out}/walnut_nor_4k.jpg', quality=92)
# page crop: 1024 x 512 px from the top-left = 250 x 125 mm at the scan's native 4.1 px/mm
for n in ('diff', 'rough', 'nor'):
    Image.open(f'{tex_out}/walnut_{n}_4k.jpg').crop((0, 0, 1024, 512)).save(f'{page_out}/walnut_{n}.jpg', quality=90)
print('graded')
