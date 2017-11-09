return {
    -- SloanDDS module
    {
        Name = "Sloan Digital Sky Survey",
        Parent = "Root",
        Renderable = {
            Type = "RenderableBillboardsCloud",
            Enabled = true,
            Color = { 0.8, 0.8, 1.0 },
            Transparency = 1.0,
            ScaleFactor = 507.88,
            File = "speck/SDSSgals.speck",
            ColorMap = "speck/lss.cmap",
            ColorOption = {"redshift", "prox5Mpc"},
            ColorRange = { { 0.0, 0.075 }, { 1.0, 50.0 } },
            Texture = "textures/point3.png",
            -- Fade in value in the same unit as "Unit"
            --FadeInThreshould = 4.7,
            FadeInThreshould = 90.0,
            Unit = "Mpc"
        },
        GuiPath = "/Universe/Galaxies"
    }
}
