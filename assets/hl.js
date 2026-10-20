/* hl.js - tiny self-hosted syntax highlighter for the Nocto docs (C/C++/HLSL/JSON/CMake/shell). No dependencies, no network. */
(function () {
  var KW = {
    c: "if else for while do return switch case default break continue struct typedef enum union static const volatile extern inline sizeof new delete template class public private namespace using auto void bool char short int long float double unsigned signed nullptr true false NULL uint32_t int32_t int64_t uint64_t size_t HRESULT register cbuffer Texture2D SamplerState float2 float3 float4 uint uint2 lerp saturate smoothstep length",
    json: "true false null",
    cmake: "cmake_minimum_required project set add_library target_include_directories target_link_libraries set_target_properties function endfunction install if endif message",
    sh: "cmake cl copy mkdir cd dir set"
  };
  function esc(s) { return s.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;"); }
  function hl(src, lang) {
    var kw = {}; (KW[lang === "hlsl" || lang === "cpp" ? "c" : lang] || "").split(" ").forEach(function (k) { if (k) kw[k] = 1; });
    var re = lang === "json"
      ? /("(?:[^"\\]|\\.)*"\s*:)|("(?:[^"\\]|\\.)*")|(-?\b\d[\d.eE+-]*\b)|(\b[a-z]+\b)/g
      : /(\/\/[^\n]*|\/\*[\s\S]*?\*\/|^\s*#[^\n]*|(?:^|\s)#\s[^\n]*)|("(?:[^"\\\n]|\\.)*"|'(?:[^'\\\n]|\\.)*'|R"\([\s\S]*?\)")|(\b0x[0-9a-fA-F]+u?\b|\b\d[\d.]*(?:[eE][+-]?\d+)?[fuU]?\b)|([A-Za-z_]\w*)/gm;
    var out = "", last = 0, m;
    while ((m = re.exec(src))) {
      out += esc(src.slice(last, m.index)); last = re.lastIndex;
      var t = m[0], cls = null;
      if (lang === "json") cls = m[1] ? "k" : m[2] ? "s" : m[3] ? "n" : (kw[t] ? "kw" : null);
      else if (m[1]) cls = /^\s*#/.test(t) && lang !== "cmake" && lang !== "sh" ? "pp" : "c";
      else if (m[2]) cls = "s"; else if (m[3]) cls = "n";
      else if (kw[t]) cls = "kw";
      else if (/^(nocto_|NOCTO_|ID3D|D3D|DXGI_)/.test(t)) cls = "t";
      else if (src.charAt(re.lastIndex) === "(") cls = "f";
      out += cls ? '<span class="' + cls + '">' + esc(t) + "</span>" : esc(t);
    }
    return out + esc(src.slice(last));
  }
  document.querySelectorAll("pre code[class*=lang-]").forEach(function (el) {
    var lang = (el.className.match(/lang-(\w+)/) || [])[1];
    el.innerHTML = hl(el.textContent, lang);
  });
  document.querySelectorAll("pre").forEach(function (p) {
    var b = document.createElement("button"); b.className = "copy"; b.textContent = "Copy";
    b.onclick = function () { navigator.clipboard.writeText(p.querySelector("code").textContent).then(function () { b.textContent = "Copied"; setTimeout(function () { b.textContent = "Copy"; }, 1200); }); };
    p.appendChild(b);
  });
  var t = document.getElementById("toc-toggle"); if (t) t.onclick = function () { document.body.classList.toggle("toc-open"); };
  document.querySelectorAll("#toc a").forEach(function (a) { a.onclick = function () { document.body.classList.remove("toc-open"); }; });
})();
