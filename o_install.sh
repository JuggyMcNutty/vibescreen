










<!doctype html>
<html class="h-full overflow-y-scroll">
  <head>
    <title>Ollama · Run AI models locally and in the cloud</title>

    <meta charset="utf-8" />
    <meta name="description" content="Ollama is the easiest way to run and build with open AI models locally or in the cloud on macOS, Windows, and Linux."/>
    <meta name="viewport" content="width=device-width, initial-scale=1" />
    <meta property="og:title" content="Ollama · Run AI models locally and in the cloud" />
    <meta property="og:description" content="Run and build with open AI models locally or in the cloud on macOS, Windows, and Linux." />
    <meta property="og:url" content="https://ollama.com" />
    <meta property="og:image" content="https://ollama.com/public/og.png" />
    <meta property="og:image:type" content="image/png" />
    <meta property="og:image:width" content="1200" />
    <meta property="og:image:height" content="628" />
    <meta property="og:image:alt" content="Ollama's black-and-white logo" />
    <meta property="og:type" content="website" />

    <meta name="robots" content="index, follow" />

    <meta property="twitter:card" content="summary" />
    <meta property="twitter:title" content="Ollama · Run AI models locally and in the cloud" />
    <meta property="twitter:description" content="Run and build with open AI models locally or in the cloud on macOS, Windows, and Linux." />
    <meta property="twitter:site" content="ollama" />

    <meta property="twitter:image:src" content="https://ollama.com/public/og-twitter.png" />
    <meta property="twitter:image:width" content="1200" />
    <meta property="twitter:image:height" content="628" />
    <meta property="twitter:image:alt" content="Ollama's black-and-white logo" />

    <meta name="viewport" content="width=device-width, initial-scale=1, maximum-scale=1">

    <link rel="icon" type="image/png" sizes="16x16" href="/public/icon-16x16.png" />
    <link rel="icon" type="image/png" sizes="32x32" href="/public/icon-32x32.png" />
    <link rel="icon" type="image/png" sizes="48x48" href="/public/icon-48x48.png" />
    <link rel="icon" type="image/png" sizes="64x64" href="/public/icon-64x64.png" />
    <link rel="apple-touch-icon" sizes="180x180" href="/public/apple-touch-icon.png" />
    <link rel="icon" type="image/png" sizes="192x192" href="/public/android-chrome-icon-192x192.png" />
    <link rel="icon" type="image/png" sizes="512x512" href="/public/android-chrome-icon-512x512.png" />

    <link rel="canonical" href="https://ollama.com/" />
    

    <link href="/public/tailwind.css?v=5fe5ed4866decb45a9200e9d5f5e93a7" rel="stylesheet" />
    <link href="/public/vendor/prism/prism.css?v=5fe5ed4866decb45a9200e9d5f5e93a7" rel="stylesheet" />
    <script type="application/ld+json">
      {
        "@context": "https://schema.org",
        "@graph": [
          {
            "@type": "Organization",
            "@id": "https://ollama.com/#organization",
            "name": "Ollama",
            "legalName": "Ollama Inc.",
            "url": "https://ollama.com",
            "logo": "https://ollama.com/public/ollama.png",
            "sameAs": ["https://github.com/ollama/ollama", "https://twitter.com/ollama"]
          },
          {
            "@type": "WebSite",
            "@id": "https://ollama.com/#website",
            "name": "Ollama",
            "url": "https://ollama.com",
            "publisher": { "@id": "https://ollama.com/#organization" }
          }
        ]
      }
    </script>

    <script type="text/javascript">
      function copyToClipboard(element) {
        let commandElement = null;
        const preElement = element.closest('pre');
        const languageNoneElement = element.closest('.language-none');

        if (preElement) {
          commandElement = preElement.querySelector('code');
        } else if (languageNoneElement) {
          commandElement = languageNoneElement.querySelector('.command');
        } else {
          const parent = element.parentElement;
          if (parent) {
            commandElement = parent.querySelector('.command');
          }
        }

        if (!commandElement) {
          console.error('No code or command element found');
          return;
        }

        const code = commandElement.textContent ? commandElement.textContent.trim() : commandElement.value;

        navigator.clipboard
          .writeText(code)
          .then(() => {
            const copyIcon = element.querySelector('.copy-icon')
            const checkIcon = element.querySelector('.check-icon')

            copyIcon.classList.add('hidden')
            checkIcon.classList.remove('hidden')

            setTimeout(() => {
              copyIcon.classList.remove('hidden')
              checkIcon.classList.add('hidden')
            }, 2000)
          })
      }
    </script>
    
    <script>
      
      function getIcon(url) {
        url = url.toLowerCase();
        if (url.includes('x.com') || url.includes('twitter.com')) return 'x';
        if (url.includes('github.com')) return 'github';
        if (url.includes('linkedin.com')) return 'linkedin';
        if (url.includes('youtube.com')) return 'youtube';
        if (url.includes('hf.co') || url.includes('huggingface.co') || url.includes('huggingface.com')) return 'hugging-face';
        return 'default';
      }

      function setInputIcon(input) {
        const icon = getIcon(input.value);
        const img = input.previousElementSibling.querySelector('img');
        img.src = `/public/social/${icon}.svg`;
        img.alt = `${icon} icon`;
      }

      function setDisplayIcon(imgElement, url) {
        const icon = getIcon(url);
        imgElement.src = `/public/social/${icon}.svg`;
        imgElement.alt = `${icon} icon`;
      }
    </script>
    
    <script src="/public/vendor/htmx/bundle.js"></script>
  </head>

  <body
    class="
      antialiased
      min-h-screen
      w-full
      m-0
      flex
      flex-col
    "
    hx-on:keydown="
      if (event.target.tagName === 'INPUT' || event.target.tagName === 'TEXTAREA') {
        // Ignore key events in input fields.
        return;
      }
      if ((event.metaKey && event.key === 'k') || event.key === '/') {
        const sp = htmx.find('#search') || htmx.find('#form-input') || htmx.find('#filter');
        if (sp) {
          event.preventDefault();
          sp.focus();
        }
      }
    "
  >
    
      
<header class="sticky top-0 z-40 flex h-20 shrink-0 items-center border-b border-transparent bg-white px-5 sm:px-6 lg:px-gutter text-black data-[scrolled]:border-black/[0.12] lg:static lg:border-b-0">
  <nav aria-label="Main navigation" class="flex w-full items-center justify-between gap-6 text-lg leading-6 tracking-tight font-normal ">
    <div class="flex min-w-0 flex-1 items-center gap-6">
      <a href="/" aria-label="Ollama home" class="-ml-0.5 flex-none"><img src="/public/ollama-nav.png" alt="Ollama logo" width="37" height="43" class="h-auto w-[37px]" /></a>
      
    </div>
    <div class="hidden items-center gap-7 md:flex">
      <a href="https://github.com/ollama/ollama" class="flex items-center gap-1.5 text-[17px]">
        <svg class="h-[18px] w-[18px] flex-none" viewBox="0 0 16 16" fill="currentColor" aria-hidden="true"><path d="M8 0c4.42 0 8 3.58 8 8a8.013 8.013 0 0 1-5.45 7.59c-.4.08-.55-.17-.55-.38 0-.27.01-1.13.01-2.2 0-.75-.25-1.23-.54-1.48 1.78-.2 3.65-.88 3.65-3.95 0-.88-.31-1.59-.82-2.15.08-.2.36-1.02-.08-2.12 0 0-.67-.22-2.2.82-.64-.18-1.32-.27-2-.27-.68 0-1.36.09-2 .27-1.53-1.03-2.2-.82-2.2-.82-.44 1.1-.16 1.92-.08 2.12-.51.56-.82 1.28-.82 2.15 0 3.06 1.86 3.75 3.64 3.95-.23.2-.44.55-.51 1.07-.46.21-1.61.55-2.33-.66-.15-.24-.6-.83-1.23-.82-.67.01-.27.38.01.53.34.19.73.9.82 1.13.16.45.68 1.31 2.69.94 0 .67.01 1.3.01 1.49 0 .21-.15.45-.55.38A7.995 7.995 0 0 1 0 8c0-4.42 3.58-8 8-8Z" /></svg>
        <span class="sr-only">GitHub</span>
        <span class="tabular-nums">184.7K</span>
      </a>
      <a href="/download" class="underline-offset-4 hover:underline">Download</a>
      <a href="/search" class="underline-offset-4 hover:underline">Models</a>
      <a href="https://docs.ollama.com" class="underline-offset-4 hover:underline">Docs</a>
      <a href="/pricing" class="underline-offset-4 hover:underline">Pricing</a>
      
        <a href="/signin" class="underline-offset-4 hover:underline">Sign in</a>
      
    </div>
    <div class="flex items-center md:hidden">
      <input type="checkbox" id="menu" class="peer hidden" />
      <label for="menu" aria-label="Open menu" class="z-50 block cursor-pointer peer-checked:hidden">
        <svg class="h-7 w-7" fill="none" viewBox="0 0 24 24" stroke-width="1.5" stroke="currentColor" aria-hidden="true"><path stroke-linecap="round" stroke-linejoin="round" d="M3.75 6.75h16.5M3.75 12h16.5m-16.5 5.25h16.5" /></svg>
      </label>
      <label for="menu" aria-label="Close menu" class="fixed right-5 sm:right-6 lg:right-8 top-[26px] z-50 hidden cursor-pointer peer-checked:block">
        <svg class="h-7 w-7" fill="none" viewBox="0 0 24 24" stroke-width="1.5" stroke="currentColor" aria-hidden="true"><path stroke-linecap="round" stroke-linejoin="round" d="M6 18L18 6M6 6l12 12" /></svg>
      </label>
      <div class="fixed inset-0 z-40 hidden overflow-y-auto bg-white peer-checked:block">
        <div class="flex flex-col space-y-5 px-5 sm:px-6 lg:px-8 pb-8 pt-[5.5rem] text-3xl">
          
          <a href="/download">Download</a>
          <a href="/search">Models</a>
          <a href="https://docs.ollama.com">Docs</a>
          <a href="/pricing">Pricing</a>
          
            <a href="/signin">Sign in</a>
          
        </div>
      </div>
    </div>
  </nav>
</header>
<script>
  (function (header) {
    function update() { header.toggleAttribute('data-scrolled', window.scrollY > 0); }
    update();
    window.addEventListener('scroll', update, { passive: true });
  })(document.currentScript.previousElementSibling);
</script>

    

    
<main class="w-full px-5 pb-16 pt-20 sm:px-6 md:pt-24 lg:px-gutter">
  <h1 class="max-w-xl text-xl font-medium leading-snug tracking-tight text-neutral-900 md:text-[1.375rem]">Ollama is the most popular way to build with open models.</h1>
  <p class="mt-4 max-w-xl text-lg leading-relaxed text-neutral-900 md:text-xl md:leading-7">Access the latest open models with complete privacy locally or in the cloud. Your prompts are never stored or trained on.</p>
  <div class="mt-12 flex flex-wrap items-center gap-3">
    <a href="/signup" class="inline-flex items-center justify-center rounded-full bg-neutral-900 px-6 py-2 text-lg text-white hover:bg-black">Get started</a>
  </div>
  <dl class="mt-16 flex max-w-xl justify-between gap-x-4 sm:gap-x-6 md:mt-20">
    <div>
      <dt class="sr-only">Monthly installs</dt>
      <dd class="text-2xl font-medium tabular-nums text-neutral-900 md:text-3xl">9M+</dd>
      <dd class="mt-1 text-xs text-neutral-500 sm:text-sm">installs a month</dd>
    </div>
    <div>
      <dt class="sr-only">Model downloads</dt>
      <dd class="text-2xl font-medium tabular-nums text-neutral-900 md:text-3xl">1B+</dd>
      <dd class="mt-1 text-xs text-neutral-500 sm:text-sm">model downloads</dd>
    </div>
    <div>
      <dt class="sr-only">Tokens served</dt>
      <dd class="text-2xl font-medium tabular-nums text-neutral-900 md:text-3xl">200T+</dd>
      <dd class="mt-1 text-xs text-neutral-500 sm:text-sm">tokens served</dd>
    </div>
  </dl>
  <div class="mt-16 grid max-w-xl grid-cols-3 gap-x-6 gap-y-6 sm:grid-cols-6 md:mt-20">
    <div class="flex h-8 items-center justify-center"><img src="/public/logos/apple.svg" alt="Apple" style="--h: 26.0" class="h-[calc(var(--h)*0.75px)] w-auto object-contain opacity-50 grayscale md:h-[calc(var(--h)*1px)]" /></div>
    <div class="flex h-8 items-center justify-center"><img src="/public/logos/nvidia.svg" alt="NVIDIA" style="--h: 15.8" class="h-[calc(var(--h)*0.75px)] w-auto object-contain opacity-50 grayscale md:h-[calc(var(--h)*1px)]" /></div>
    <div class="flex h-8 items-center justify-center"><img src="/public/logos/microsoft.svg" alt="Microsoft" style="--h: 16.4" class="h-[calc(var(--h)*0.75px)] w-auto object-contain opacity-50 grayscale md:h-[calc(var(--h)*1px)]" /></div>
    <div class="flex h-8 items-center justify-center"><img src="/public/logos/meta.svg" alt="Meta" style="--h: 23.0" class="h-[calc(var(--h)*0.75px)] w-auto object-contain opacity-50 grayscale md:h-[calc(var(--h)*1px)]" /></div>
    <div class="flex h-8 items-center justify-center"><img src="/public/logos/adobe.svg" alt="Adobe" style="--h: 25.1" class="h-[calc(var(--h)*0.75px)] w-auto object-contain opacity-50 grayscale md:h-[calc(var(--h)*1px)]" /></div>
    <div class="flex h-8 items-center justify-center"><img src="/public/logos/intel.svg" alt="Intel" style="--h: 19.6" class="h-[calc(var(--h)*0.75px)] w-auto object-contain opacity-50 grayscale md:h-[calc(var(--h)*1px)]" /></div>
    <div class="flex h-8 items-center justify-center"><img src="/public/logos/nasa.svg" alt="NASA" style="--h: 17.7" class="h-[calc(var(--h)*0.75px)] w-auto object-contain opacity-50 grayscale md:h-[calc(var(--h)*1px)]" /></div>
    <div class="flex h-8 items-center justify-center"><img src="/public/logos/netflix.svg" alt="Netflix" style="--h: 26.0" class="h-[calc(var(--h)*0.75px)] w-auto object-contain opacity-50 grayscale md:h-[calc(var(--h)*1px)]" /></div>
    <div class="flex h-8 items-center justify-center"><img src="/public/logos/ibm.svg" alt="IBM" style="--h: 19.4" class="h-[calc(var(--h)*0.75px)] w-auto object-contain opacity-50 grayscale md:h-[calc(var(--h)*1px)]" /></div>
    <div class="flex h-8 items-center justify-center"><img src="/public/logos/nike.svg" alt="Nike" style="--h: 19.0" class="h-[calc(var(--h)*0.75px)] w-auto object-contain opacity-50 grayscale md:h-[calc(var(--h)*1px)]" /></div>
    <div class="flex h-8 items-center justify-center"><img src="/public/logos/visa.svg" alt="Visa" style="--h: 18.5" class="h-[calc(var(--h)*0.75px)] w-auto object-contain opacity-50 grayscale md:h-[calc(var(--h)*1px)]" /></div>
    <div class="flex h-8 items-center justify-center"><img src="/public/logos/mercedes.svg" alt="Mercedes-Benz" style="--h: 26.0" class="h-[calc(var(--h)*0.75px)] w-auto object-contain opacity-50 grayscale md:h-[calc(var(--h)*1px)]" /></div>
  </div>
</main>


    
      
<footer class="mt-auto underline-offset-4">
  <div class="flex flex-col items-center justify-center px-4 py-4 md:flex-row md:justify-between md:px-6 md:py-3.5 lg:px-gutter">
    <div class="order-2 mt-2 text-sm text-neutral-500 md:order-1 md:mt-0 md:text-xs">&copy; 2026 Ollama</div>
    <ul data-footer-links class="order-1 flex flex-wrap items-center justify-center text-sm text-neutral-500 md:order-2 md:gap-x-6 md:text-xs">
      <li class="mx-2 my-1 md:mx-0 md:my-0">
        <a href="/blog" class="hover:underline">Blog</a>
      </li>
      <li class="mx-2 my-1 md:mx-0 md:my-0">
        <a href="https://docs.ollama.com" class="hover:underline">Docs</a>
      </li>
      <li class="mx-2 my-1 md:mx-0 md:my-0">
        <a href="https://github.com/ollama/ollama" class="hover:underline">GitHub</a>
      </li>
      <li class="mx-2 my-1 md:mx-0 md:my-0">
        <a href="https://discord.com/invite/ollama" class="hover:underline">Discord</a>
      </li>
      <li class="mx-2 my-1 md:mx-0 md:my-0">
        <a href="https://twitter.com/ollama" class="hover:underline">X (Twitter)</a>
      </li>
      <li class="mx-2 my-1 md:mx-0 md:my-0">
        <a href="mailto:support@ollama.com" class="hover:underline">Support</a>
      </li>
      <li class="mx-2 my-1 md:hidden">
        <a href="https://lu.ma/ollama" class="hover:underline">Meetups</a>
      </li>
      <li class="mx-2 my-1 md:mx-0 md:my-0">
        <a href="https://jobs.ashbyhq.com/ollama" class="hover:underline">Careers</a>
      </li>
      <li class="mx-2 my-1 md:mx-0 md:my-0">
        <a href="/privacy" class="hover:underline">Privacy</a>
      </li>
      <li class="mx-2 my-1 md:mx-0 md:my-0">
        <a href="/terms" class="hover:underline">Terms</a>
      </li>
    </ul>
  </div>
</footer>

    

    
    <span class="hidden" id="end_of_template"></span>
  </body>
</html>
