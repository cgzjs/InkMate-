const fs = require('node:fs');
const path = require('node:path');
const assert = require('node:assert/strict');
const {chromium} = require(process.env.PLAYWRIGHT_MODULE || 'playwright');
const root = path.resolve(__dirname, '..');
const source = fs.readFileSync(path.join(root, path.basename(root) + '.ino'), 'utf8');
const handler = source.split('void handleWebRoot() {')[1].split('\nvoid ')[0];
const chunks = [...handler.matchAll(/R"HTML\(([\s\S]*?)\)HTML"/g)].map(m => m[1]);
assert.equal(chunks.length, 2);
const metrics = `<div class="metric"><span>电池状态</span><strong>80%</strong></div>
<div class="metric"><span>设备信息</span><strong>v2.8.0 · 150 KB 内存可用</strong></div>
<div class="metric"><span>Flash 容量</span><strong id="flashCapacity">--</strong></div>
<div class="metric"><span>内容存储</span><strong id="storageSummary">--</strong></div></div>`;
const html = chunks[0] + metrics + chunks[1];
const output = path.join(root, 'build/storage_tests');
fs.mkdirSync(output, {recursive:true});
fs.writeFileSync(path.join(output, 'web_actual.html'), html);

(async () => {
 const browser = await chromium.launch({headless:true,channel:process.env.PLAYWRIGHT_BROWSER_CHANNEL || undefined});
 const results = [];
 for (const profile of ['4MB', '16MB', '16MB_legacy']) {
  const large = profile === '16MB';
  const status = {
   flashBytes: profile === '4MB' ? 4 * 1024 * 1024 : 16 * 1024 * 1024,
   storageTotal:large ? 0xCA0000 : 0xA0000,
   storageUsed:200 * 1024, storageFree:large ? 12 * 1024 * 1024 : 440 * 1024,
   storageReady:true, bookMaxBytes:large ? 8 * 1024 * 1024 : 256 * 1024,
   vocabMaxBytes:large ? 2 * 1024 * 1024 : 256 * 1024,
   vocabMaxWords:large ? 20000 : 5000, expansionAvailable:profile === '16MB_legacy'
  };
  for (const width of [1280, 390, 320]) {
   const page = await browser.newPage({viewport:{width,height:900}});
   const errors = [], uploaded = [];
   page.on('pageerror', error => errors.push(error.message));
   page.on('dialog', dialog => dialog.accept());
   await page.route('http://fortunepager.test/**', async route => {
    const request = route.request(), url = new URL(request.url());
    const json = body => route.fulfill({contentType:'application/json',body:JSON.stringify(body)});
    if (url.pathname === '/') return route.fulfill({contentType:'text/html; charset=utf-8',body:html});
    if (url.pathname === '/status') return json(status);
    if (url.pathname === '/features') return json([]);
    if (url.pathname.endsWith('-options')) return json({builtins:[],custom:[]});
    if (url.pathname === '/todos') return route.fulfill({body:''});
    if (url.pathname === '/vocab') return json({deck:0,daily:10,installed:true,learned:4,total:2000,due:2,todayDone:1,todayNew:1});
    if (url.pathname === '/water') return json({cup:250,goal:2000,cups:1,total:250});
    if (url.pathname === '/mood') return json({index:1});
    if (['/book','/vocab-file'].includes(url.pathname)) {
     if (request.method() === 'POST') {
      assert.match(request.headers()['content-type'], /^multipart\/form-data;/);
      uploaded.push({path:url.pathname,bytes:request.postDataBuffer().length});
     }
     return route.fulfill({body:'ok'});
    }
    throw new Error('Unexpected URL ' + url.pathname);
   });
   await page.goto('http://fortunepager.test/');
   await page.waitForFunction(() => document.getElementById('storageSummary').textContent.includes(' / '));
   assert.equal(await page.locator('#flashCapacity').innerText(), profile === '4MB' ? '4 MB' : '16 MB');
   const layout = await page.evaluate(() => ({width:innerWidth,scroll:document.documentElement.scrollWidth,
    outside:[...document.querySelectorAll('body *')].filter(e=>e.getBoundingClientRect().right>innerWidth+1)
      .map(e=>({tag:e.tagName,id:e.id,text:e.textContent.slice(0,60),right:e.getBoundingClientRect().right}))}));
   if (layout.scroll>layout.width) console.log(profile,width,layout);
   await page.screenshot({path:path.join(output, `web_${profile}_${width}_home.png`),fullPage:true});
   assert.equal(layout.scroll <= layout.width, true);
   await page.locator('[data-tab=content]').click();
   await page.locator('#bookFile').setInputFiles({name:'large.txt',mimeType:'text/plain',buffer:Buffer.alloc(300*1024,97)});
   await page.locator('#bookSend').click();
   if (large) {
    await page.waitForFunction(() => document.getElementById('bookStatus').textContent.includes('上传完成'));
    assert.equal(uploaded.length,1); assert.ok(uploaded[0].bytes > 60*1024);
    await page.locator('#vocabFile').setInputFiles({name:'words.tsv',mimeType:'text/plain',buffer:Buffer.from('hello\t你好\nworld\t世界\n')});
    await page.locator('#vocabSend').click();
    await page.waitForFunction(() => document.getElementById('vocabFileStatus').textContent.includes('共 2 词'));
    assert.equal(uploaded[1].path, '/vocab-file');
   } else {
    await page.waitForFunction(() => document.getElementById('bookStatus').textContent.includes('超过本次上限'));
    assert.equal(uploaded.length,0);
   }
   assert.equal(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth), true);
   await page.screenshot({path:path.join(output, `web_${profile}_${width}.png`),fullPage:true});
   await page.locator('[data-tab=system]').click();
   assert.equal((await page.locator('#storageWarning').innerText()).includes('旧分区'),profile === '16MB_legacy');
   assert.deepEqual(errors, []);
   results.push({profile,width,errors,uploads:uploaded});
   await page.close();
  }
 }
 await browser.close();
 fs.writeFileSync(path.join(output,'web_results.json'),JSON.stringify(results,null,2));
 console.log('PASS: 4MB, 16MB and legacy partition UI; 1280/390/320px; multipart uploads; limits; no JS errors/overflow');
})().catch(error => { console.error(error); process.exit(1); });
