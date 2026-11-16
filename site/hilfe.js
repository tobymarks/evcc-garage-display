// Hilfe: Anker öffnen die passende Frage, das Suchfeld filtert die Fragen
const faq = document.getElementById("fragen");
const items = [...faq.querySelectorAll("details")];
const groups = [...faq.querySelectorAll(".faq-group")];
const empty = document.getElementById("faq-empty");
const filter = document.getElementById("faq-filter");

function openFromHash() {
  const id = decodeURIComponent(location.hash.slice(1));
  const target = id && document.getElementById(id);
  if (!target) return;
  const details = target.closest("details");
  if (details) {
    // Ein Filter könnte die Frage verstecken
    if (filter.value) {
      filter.value = "";
      applyFilter();
    }
    details.open = true;
  }
  target.scrollIntoView({ block: "start" });
}

// Verlinkt man auf eine Frage, steht ihr Anker in der Adresse – so lässt sie sich weitergeben
items.forEach((d) =>
  d.addEventListener("toggle", () => {
    if (d.open && location.hash !== "#" + d.id) history.replaceState(null, "", "#" + d.id);
  })
);

const norm = (s) => s.toLocaleLowerCase("de").normalize("NFKD").replace(/[̀-ͯ]/g, "");

function applyFilter() {
  const words = norm(filter.value).split(/\s+/).filter(Boolean);
  let shown = 0;
  items.forEach((d) => {
    const text = norm(d.textContent);
    const hit = words.every((w) => text.includes(w));
    d.hidden = !hit;
    if (hit) shown++;
  });
  groups.forEach((g) => (g.hidden = !g.querySelector("details:not([hidden])")));
  empty.hidden = shown > 0;
}

filter.addEventListener("input", applyFilter);
window.addEventListener("hashchange", openFromHash);
openFromHash();
