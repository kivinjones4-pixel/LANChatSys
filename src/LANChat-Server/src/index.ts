import net from 'net';
import readline from 'readline';

const PORT = 8888;
const clients: net.Socket[] = [];

const server = net.createServer((socket) => {
  console.log(`Client connected from ${socket.remoteAddress}:${socket.remotePort}`);
  clients.push(socket);
  
  // 发送欢迎消息
  socket.write('Welcome to the LAN Chat Server!\n');
  socket.write('Enter your username: ');
  
  let username = `User${clients.length}`;
  let gotUsername = false;
  
  socket.on('data', (data) => {
    const message = data.toString().trim();
    
    if (!gotUsername) {
      username = message || username;
      gotUsername = true;
      socket.write(`Hello ${username}!\n`);
      broadcast(`${username} joined the chat`, socket);
      return;
    }
    
    console.log(`${username}: ${message}`);
    broadcast(`${username}: ${message}`, socket);
  });
  
  socket.on('end', () => {
    console.log(`${username} disconnected`);
    const index = clients.indexOf(socket);
    if (index > -1) clients.splice(index, 1);
    broadcast(`${username} left the chat`, socket);
  });
  
  socket.on('error', (err) => {
    console.error(`Socket error for ${username}:`, err.message);
  });
});

function broadcast(message: string, sender?: net.Socket) {
  clients.forEach(client => {
    if (client !== sender && !client.destroyed) {
      client.write(`${message}\n`);
    }
  });
}

// 启动服务器
server.listen(PORT, () => {
  console.log(`Server listening on port ${PORT}`);
  console.log('Commands: /users - List clients, /stop - Stop server');
});

// 命令行界面
const rl = readline.createInterface({
  input: process.stdin,
  output: process.stdout
});

rl.on('line', (input) => {
  const command = input.trim();
  
  if (command === '/users') {
    console.log(`Connected clients: ${clients.length}`);
  } else if (command === '/stop') {
    console.log('Shutting down server...');
    server.close();
    clients.forEach(client => client.destroy());
    rl.close();
    process.exit(0);
  } else if (command.startsWith('/say ')) {
    const message = command.substring(5);
    broadcast(`[Server]: ${message}`);
  } else {
    console.log('Unknown command. Try: /users, /stop, /say <message>');
  }
});

console.log('Type "/help" for commands');