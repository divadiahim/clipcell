<h1>Clipcell</h3>

<!-- TABLE OF CONTENTS -->
<details>
  <summary>Table of Contents</summary>
  <ol>
    <li>
      <a href="#about-the-project">About The Project</a>
    </li>
    <li>
      <a href="#getting-started">Getting Started</a>
      <ul>
        <li><a href="#prerequisites">Prerequisites</a></li>
        <li><a href="#installation">Installation</a></li>
      </ul>
    </li>
    <li><a href="#usage">Usage</a></li>
    <li><a href="#roadmap">Roadmap</a></li>
    <li><a href="#acknowledgments">Acknowledgments</a></li>
  </ol>
</details>



<!-- ABOUT THE PROJECT -->
## About The Project
A simple wayland native GUI clipboard manager, with support for text and image preview.

[![Product Name Screen Shot][product-screenshot]](https://example.com)



<p align="right">(<a href="#readme-top">back to top</a>)</p>



<!-- GETTING STARTED -->
## Getting Started

### Prerequisites
* libpng
* freetype2
* libxkbcommon
* libmagic
* wayland (obviously)
* wl-clipboard

### Configuration

1. Configuration
   In suckless fashion configuring the clipboard is done only through the src/config.h file.
   To keep everything minimal and not bloat the project with unnecessary fontconfig dependencies a path to the font that is to be used should be set in the config file, by default it being ``/usr/share/fonts/koruri/Koruri-Regular.ttf``.

### Installation
#### From AUR
  ```sh
  yay -S clipcell
  ```
#### From Source
Just a simple
   ```sh
   sudo make install
   ```
   should install clipcelld and clipcell to /usr/bin.

<p align="right">(<a href="#readme-top">back to top</a>)</p>

<!-- USAGE EXAMPLES -->
## Usage

The clipboard entries are stored in a shared memory file, created by the server. The client does not run if the file does not exist.

1. To store clipboard entries just run the server like so:
   ```sh
   wl-paste --watch clipcelld store
   ```
   This shall be called once per session in your config.

2. To delete the saved clipboard entries (including the shm file) run:
   ```sh
   clipcelld reset
   ```
3. Once the server has created a shm file, the client can simply be run as such:
   ```sh
   clipcell | wl-copy
   ```

<p align="right">(<a href="#readme-top">back to top</a>)</p>

<!-- ROADMAP -->
## Roadmap

- [ ] Ability to render any type of image (not just png)
- [ ] Better font rendering
- [ ] Mouse scrolling

<p align="right">(<a href="#readme-top">back to top</a>)</p>

<!-- ACKNOWLEDGMENTS -->
## Acknowledgments

* [foot](https://codeberg.org/dnkl/foot) - *For teaching me about timers*
* [cliphist](https://github.com/sentriz/cliphist) - *For inspiring this project*

<p align="right">(<a href="#readme-top">back to top</a>)</p>

[product-screenshot]: images/screenshot.png
