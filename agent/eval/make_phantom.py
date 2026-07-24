import SimpleITK as sitk, numpy as np, sys
n=128; c=n//2
zz,yy,xx=np.mgrid[0:n,0:n,0:n]
a=np.full((n,n,n),40,dtype=np.float32)                 # background=40
a[(xx-c)**2+(yy-c)**2+(zz-c)**2 < 45**2]=100           # organ=100
a[(xx-c-15)**2+(yy-c+10)**2+(zz-c)**2 < 9**2]=220       # nodule=220 (clean, no noise)
img=sitk.GetImageFromArray(a); img.SetSpacing((1.0,1.0,1.0))
out=sys.argv[1]; sitk.WriteImage(img, out); print("wrote",out)
