# LoryVox

A 3D rotational persistence-of-vision (POV) volumetric display built on a Raspberry Pi 4, driven by two HUB75E RGB LED panels spinning on a custom 3D-printed rotor. The display synthesises a three-dimensional image by presenting different 2D slices at precise angular positions as the panels rotate, exploiting the visual system's persistence of vision to integrate them into a static 3D percept.

This is a final-year EEE project at UCL (2025).

---

## Hardware

| Component | Specification |
|-----------|--------------|
| Single-board computer | Raspberry Pi 4 (4 GB) |
| LED panels | 2× P2.5 64×64 HUB75E (ICN2037 driver) |
| Motor | RS PRO 216-3789 BLDC, 24 V, 4800 RPM rated |
| Transmission | GT2 belt and pulley |
| Slip ring | ASL9013 2-line, 24 V |
| Brush holder | AS-PL ABH6004S carbon brush |
| Synchronisation | Photo-interrupter (half-disc flag, 1 pulse/rev) |
| PCB adapter | Custom 2-layer HUB75E level-shifter (74HCT245 × 2) |
| Power | 24 V SMPS → on-rotor 24 V→5 V DC-DC converter |
| Structure | FDM 3D-printed PLA (structural) + PETG (motor damper) |

---

## Repository Layout

```
├── src
│   ├── driver
│   │   ├── gadgets         -- hardware configurations (GPIO mapping, panel layout)
│   │   └── vortex.c        -- DMA-driven display driver, shared-memory voxel buffer
│   ├── simulator
│   │   └── virtex.c        -- OpenGL software simulator (no hardware needed)
│   ├── multivox            -- launcher / front end
│   ├── platform            -- common client code
│   └── toys
│       ├── tesseract.c     -- 4D hypercube demo
│       ├── viewer.c        -- OBJ / PNG file viewer with zoom-fit
│       └── ...             -- other bundled demos
├── python
│   ├── obj2c.py            -- embed .obj models in a header file
│   └── ...
└── README.md
```

---

## Building

Clone the repository on the Raspberry Pi:

```bash
git clone https://github.com/LoryZhong/LoryVox.git
cd LoryVox
mkdir build && cd build
cmake -DMULTIVOX_GADGET=vortex ..
cmake --build .
```

---

## Running

Start the display driver (requires root for DMA/GPIO access):

```bash
sudo ./vortex
```

Then launch a demo in a second terminal:

```bash
./tesseract
```

### OBJ Viewer

Load any `.obj` or `.png` file with automatic zoom-to-fit:

```bash
./viewer path/to/model.obj
```

| Key | Gamepad | Effect |
|-----|---------|--------|
| esc | — | Exit |
| [ / ] | LB / RB | Cycle models |
| — | X | Zoom to fit |
| — | Y | Toggle wireframe |

---

## Simulator

Run the OpenGL simulator without physical hardware:

```bash
./virtex
```

Useful options:

| Option | Effect |
|--------|--------|
| `-s X` | Angular slice count per revolution |
| `-w X Y` | Panel resolution |
| `-b X` | Bits per channel (1–3) |
| `-g l` | Linear scan geometry (higher quality) |

---

## Custom Gadget Configuration

GPIO pin mapping, panel dimensions, and photo-interrupter pin are defined in:

```
src/driver/gadgets/gadget_<name>.h
```

Edit this file to match your hardware wiring before building.

---

## Acknowledgements

Built on the [Multivox](https://github.com/AncientJames/multivox) open-source volumetric display framework by AncientJames. The DMA display driver, shared-memory voxel buffer architecture, OpenGL simulator, and launcher are derived from that project.
