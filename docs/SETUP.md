# Setting up Atmospheric

## 1. The weather relay on Railway (about 5 minutes)

The plugin never contains your OpenWeatherMap key. It asks a tiny server you own (`server/` in this repo), and that server holds the key.

1. In Railway: **New Project → Deploy from GitHub repo → arenttodal/weather-synth**. If the repo isn't listed, use "Configure GitHub App" to give Railway access to it.
2. Open the new service, go to **Settings → Source**:
   - **Root Directory:** `/server`
   - **Branch:** `claude/vibrant-wright-7kfl4a` (switch to `main` once this is merged)
3. **Variables** tab → **New Variable**:
   - `OWM_API_KEY` = your OpenWeatherMap key
   - optional `IPINFO_TOKEN`, only if you sign up at ipinfo.io. Without it the relay uses ip-api.com's free tier to find the user's city.
4. **Settings → Networking → Generate Domain**. You get something like `https://atmospheric-relay-production.up.railway.app`.
5. Check it in a browser:
   - `…/health` should show `{"ok":true,"keyConfigured":true}`
   - `…/v1/sky?lat=63.43&lon=10.39` should show Trondheim's weather right now
6. Send me that domain. It isn't secret. I put it in `plugin/relay-url.txt`, and the next build has it baked in.

To try it before a rebuild, put the domain in the plugin's settings file and reopen the plugin:

```
~/Library/Application Support/Atmospheric/settings.json
{ "relayUrl": "https://YOUR-DOMAIN.up.railway.app" }
```

### What the relay does
- `GET /v1/sky`: weather where the caller is, placed by their IP address (city level).
- `GET /v1/sky?lat=…&lon=…`: weather at a point (city override, globe pins).
- `GET /v1/geocode?q=Bergen`: city search for the Place button.
- `GET /health`: whether the key is configured.

It rounds every location to a ~11 km cell and caches each cell for 20 minutes, so OpenWeatherMap calls grow with the number of *places*, not users. Each IP gets 60 requests per 5 minutes. Nothing is stored on disk.

Cost: a service this small fits inside Railway's Hobby plan usage. OpenWeatherMap's free plan allows 60 calls/minute and 1,000,000/month.

## 2. Getting a build onto your Mac

Every push builds the plugin on GitHub's Mac, Windows and Linux machines, runs the tests, pluginval and Apple's `auval`, then publishes the files on the repo's Releases page under **latest**.

1. Go to github.com/arenttodal/weather-synth → **Releases** → **latest** → download `Atmospheric-macOS.zip`.
2. Unzip it, right-click **Install.command → Open**, then click Open again (macOS asks because the build isn't signed by Apple).
3. Rescan plugins in your DAW. Atmospheric is an instrument (VST3 and AU); the standalone app is in `~/Applications`.

Apple Silicon and Intel Macs are both covered (universal binary, macOS 11+).

## 3. The admin globe

With the plugin window focused:
- **Cmd+Shift+G** opens the globe in any host.
- **Cmd+W** also works where the host doesn't take it first. Most DAWs use Cmd+W to close the window, so it reliably works only in the standalone app.
- **Triple-click the sky card** works everywhere.

Drag to spin, scroll to zoom, click to hear that place's sky right now. "Keep this Day" saves the preview to your Days, and kept Days show as white dots. "Back to my sky" returns to your own weather.
