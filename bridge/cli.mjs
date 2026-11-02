import { readFile } from 'node:fs/promises';
import { parseArgs } from 'node:util';
import { connectServers, toolChat } from './mcp-client.mjs';

const { values } = parseArgs({ options: {
  config: { type: 'string', default: 'bridge/mcp.json' }, xbox: { type: 'string' },
  prompt: { type: 'string' }, list: { type: 'boolean' }, call: { type: 'string' },
  arguments: { type: 'string', default: '{}' }, 'allow-tools': { type: 'string', default: '' },
} });
let clients;
try {
  const config = JSON.parse(await readFile(values.config, 'utf8'));
  clients = await connectServers(config);
  if (values.list) {
    for (const [server, client] of clients) for (const tool of await client.tools())
      console.log(JSON.stringify({ ...tool, name: `${server}/${tool.name}` }));
  } else if (values.call) {
    const slash = values.call.indexOf('/');
    const client = clients.get(values.call.slice(0, slash));
    if (slash < 1 || !client) throw new Error('Expected --call server/tool');
    const args = JSON.parse(values.arguments);
    if (!args || typeof args !== 'object' || Array.isArray(args)) throw new Error('Expected arguments object');
    console.log(JSON.stringify(await client.call(values.call.slice(slash + 1), args), null, 2));
  } else if (values.prompt && (values.xbox || config.xbox)) {
    console.log(await toolChat({ xbox: values.xbox || config.xbox, clients, prompt: values.prompt,
      allow: values['allow-tools'].split(',').filter(Boolean) }));
  } else throw new Error('Use --list, --call server/tool, or --xbox http://CONSOLE --prompt "message"');
} catch (error) {
  console.error(error.message); process.exitCode = 1;
} finally { if (clients) await Promise.all([...clients.values()].map(client => client.close())); }
