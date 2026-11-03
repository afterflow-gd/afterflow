"""Render a before/after preview of the mobile pause-menu AF button.

The 'after' half mirrors makePauseButtonFace() in src/main.cpp: the halo is the
logo sprite stacked five times, accent tinted, additively blended, each copy
scaled up by 1 + 0.042 * ring. Run it again if those numbers change.
"""

from PIL import Image, ImageChops, ImageDraw, ImageFont

LOGO = "resources/afterflow-logo.png"
OUT = "tools/pause-button-preview.png"

ACCENT = (168, 85, 247)       # mod.json ui-primary-color default
BACKGROUND = (10, 14, 30)     # pause scrim over a level
SCALE = 3
D = 150 * SCALE               # settings-button content size on a phone


def load_logo(side):
    logo = Image.open(LOGO).convert("RGBA")
    return logo.resize((side, side), Image.LANCZOS)


def tinted(logo, alpha, color):
    """Multiply the sprite by color and fade it to alpha, keeping its alpha."""
    r, g, b, a = logo.split()
    channels = [Image.eval(c, lambda v, k=k: v * k // 255) for c, k in zip((r, g, b), color)]
    faded = Image.eval(a, lambda v: v * alpha // 255)
    return Image.merge("RGBA", (*channels, faded))


def add_over(canvas, layer):
    """Additive: out = canvas + layer.rgb * layer.alpha."""
    alpha = layer.getchannel("A")
    add = Image.merge("RGB", [ImageChops.multiply(c, alpha) for c in layer.split()[:3]])
    return ImageChops.add(canvas, add)


def draw_before(canvas, cx, cy):
    draw = ImageDraw.Draw(canvas, "RGBA")
    radius = D // 2
    draw.ellipse(
        [cx - radius + SCALE, cy - radius + SCALE, cx + radius - SCALE, cy + radius - SCALE],
        fill=(22, 22, 26, 235),
        outline=(*ACCENT, 255),
        width=2 * SCALE,
    )
    side = int(D * 0.72)
    base = load_logo(side)
    canvas.alpha_composite(base, (cx - side // 2, cy - side // 2))


def draw_after(canvas, cx, cy):
    side = int(D * 0.9)
    base = load_logo(side)
    rgb = canvas.convert("RGB")
    for ring in range(5, 0, -1):
        grown = int(side * (1.0 + 0.048 * ring))
        copy = tinted(base.resize((grown, grown), Image.LANCZOS), 14 + (5 - ring) * 14, ACCENT)
        # Pasting through the alpha mask onto black premultiplies the sprite,
        # which is exactly what drawNode/GL_SRC_ALPHA + GL_ONE adds.
        add = Image.new("RGB", canvas.size, (0, 0, 0))
        add.paste(copy, (cx - grown // 2, cy - grown // 2), copy)
        rgb = ImageChops.add(rgb, add)
    canvas = rgb.convert("RGBA")
    canvas.alpha_composite(base, (cx - side // 2, cy - side // 2))
    return canvas


def main():
    width, height = D * 3, D * 2
    canvas = Image.new("RGBA", (width, height), (*BACKGROUND, 255))

    draw_before(canvas, D, D)
    canvas = draw_after(canvas, D * 2, D)

    label = ImageFont.truetype("C:/Windows/Fonts/arialbd.ttf", 34)
    draw = ImageDraw.Draw(canvas)
    for x, text in ((D, "before"), (D * 2, "after")):
        draw.text((x, D * 2 - 40), text, font=label, fill=(190, 190, 205, 255), anchor="mm")

    canvas.convert("RGB").save(OUT)
    print("wrote", OUT, canvas.size)


if __name__ == "__main__":
    main()
