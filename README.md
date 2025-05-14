- Documentation and website under progress

# The FusionEdge

![Img1](result_images/WhatsApp Image 2025-05-14 at 11.04.51 (1).jpeg)
![Fuzzel](result_images/WhatsApp Image 2025-05-14 at 11.04.51 (2).jpeg)
![Stacking](result_images/WhatsApp Image 2025-05-14 at 11.04.51 (3).jpeg)
![Tiling](result_images/WhatsApp Image 2025-05-14 at 11.04.51 (4).jpeg)


# Building an Advanced TinyWL System

### What we have done
- Successfully cloned the repository and resolved all existing path issues.
- Cloned multiple unavailable libraries and linked them with the main files and directories.
- Replicated the setup and documented the process thoroughly.
- Implemented functionality to execute multiple windows in the TinyWL compositor with stacking behavior.
- Edited and fixed the maximize and minimize code to ensure proper functionality.
- Achieved tiling behavior with automatic positioning: centralized positioning for a single window and adjacent stacking for multiple windows.
- Developed a dynamic storage structure to manage the position and size of windows, enabling proper minimization functionality.
- Implemented an advanced snapping feature with pixel proximity checks and timing adjustments when windows come into proximity.
- Enabled windows to open inside the TinyWL compositor without requiring explicit commands for those applications.
- Integration of **Fuzzle** within the TinyWL compositor by implementation of **layer_shell_protocol** from scratch.
- Integration of two other protocols - foreign_toplevel_management protocol and DMA_BUF protocol.
- Added certain UI based features such as window cycling, keybinding based snapping, history tracking and reversal for window placements.
- Addition of windowbar.

### How to use
- Need to clone the repository.
- run:
```
rm -rf build
meson setup build
```

- It will list down all the dependencies that are required, and based on the returned errors, need to install the correct version of the libraries either using package installer or by manually cloning from git and building them.
- Once all the conflicts have been resolved (it might still show NO for some libraries)
- Run ``` ninja -C build ```

- After this has been done, navigate to the folder tinywl
- Execute

```
make clean
make
```

- Once this is successfully done, disable the graphical environment.\
- Run:
```
sudo chvt 3
```

- Again navigate to the wlroots/tinywl directory and run ``` ./tinywl ```

This will start the tinywl environment.

