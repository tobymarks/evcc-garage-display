// Hero: wechselt durch echte Screenshots vom Gerät
const heroShots = [
  ["charging-dark", "Lädt mit 62 % Sonne"],
  ["solar-light", "Nur Sonne – 100 % vom Dach"],
  ["start-dark", "Genug Sonne – gleich geht's los"],
  ["fast-light", "Schnell: volle Leistung"],
  ["nocar-dark", "Kein Fahrzeug – Modus vorwählen"],
];
const heroImg = document.getElementById("hero-screen");
const heroCaption = document.getElementById("hero-caption");
const reducedMotion = window.matchMedia("(prefers-reduced-motion: reduce)").matches;

heroShots.forEach(([name]) => {
  new Image().src = `img/${name}.png`;
});

let heroIndex = 0;
if (!reducedMotion) {
  setInterval(() => {
    heroIndex = (heroIndex + 1) % heroShots.length;
    const [name, caption] = heroShots[heroIndex];
    heroImg.classList.add("fade");
    setTimeout(() => {
      heroImg.src = `img/${name}.png`;
      heroImg.alt = caption;
      heroCaption.textContent = caption;
      heroImg.classList.remove("fade");
    }, 350);
  }, 3600);
}

// Galerie: Hell/Dunkel umschalten
const gallery = document.getElementById("ansichten");
const buttons = gallery.querySelectorAll(".theme-switch button");

function setTheme(theme) {
  gallery.classList.toggle("light", theme === "light");
  buttons.forEach((b) => b.setAttribute("aria-pressed", String(b.dataset.theme === theme)));
  gallery.querySelectorAll("img[data-shot]").forEach((img) => {
    img.src = `img/${img.dataset.shot}-${theme}.png`;
    img.loading = "lazy";
    img.width = img.height = 480;
  });
}

buttons.forEach((b) => b.addEventListener("click", () => setTheme(b.dataset.theme)));
setTheme("dark");

// Firmware-Version aus dem Manifest anzeigen
fetch("firmware/manifest.json")
  .then((r) => (r.ok ? r.json() : null))
  .then((m) => {
    if (m?.version) document.querySelector("#fw-version code").textContent = m.version;
  })
  .catch(() => {});
