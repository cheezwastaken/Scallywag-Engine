x86, x64, and ARM64 are all processor architectures.
Most of you will have x64, unless your computer is 20 years old or has a snapdragon.

Note: This uses git LFS for the installers. If you currently only see very small files instead of exe files, that is why. Run the git lfs installer, and then run git lfs pull. This will get you the installer files.

How do I check it? Simple. Assuming you have windows, here are the steps:


1. Open settings, click system, and find the tab that says about.
 
In about, you will see your system specs(ram, graphics card etc.)

2. Under device specifications, look at system type.

If it is "64-bit operating system, x64-based processor" then congratulations, you have x64.
If it is "64-bit operating system, ARM-based processor" then you have ARM64.
If it is "32-bit operating system, x86-based processor" then tough luck, you cant develop this with the tools provided and you'll have to figure out a way on your own.


Open the appropriate folder and run the EXE files.
When installing Git, check the option to add it to PATH.
When installing vs_BuildTools, check the "Desktop Development with c++" option.
Once done, you should be able to move onto the guides folder, and learn how to use git. If you already know how git works, it wouldn't hurt for a refresher, but you are free to skip. Vs code and a basic C++ guide are also in there.

-Tfola54