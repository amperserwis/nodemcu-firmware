-- Gossip protocol implementation
-- https://github.com/alexandruantochi/
local gossip = {};
local constants = {};
local utils = {};
local network = {};
local state = {};

-- Utils

utils.debug = function(message)
  if gossip.config.debug then
    if gossip.config.debugOutput then
      gossip.config.debugOutput(message);
    else
      print(message);
    end
  end
end

utils.getNetworkState = function() return sjson.encode(gossip.networkState); end
utils.getSeedList = function() return sjson.encode(gossip.config.seedList); end

utils.isNodeDataValid = function(nodeData)
  return (nodeData and nodeData.revision and nodeData.heartbeat and nodeData.state) ~= nil;
end

utils.compare = function(first, second)
  if first > second then return -1; end
  if first < second then return 1; end
  return 0;
end

utils.compareNodeData = function(first, second)
  local firstDataValid = utils.isNodeDataValid(first);
  local secondDataValid = utils.isNodeDataValid(second);

  if firstDataValid and secondDataValid then
    for index in ipairs(constants.comparisonFields) do
      local comparisonResult = utils.compare(
                                   first[constants.comparisonFields[index]],
                                   second[constants.comparisonFields[index]]);
      if comparisonResult ~= 0 then return comparisonResult; end
    end
  elseif firstDataValid then
    return -1;
  elseif secondDataValid then
    return 1;
  end
  return 0;
end

utils.getUpdateDiffDelta = function(synData)
  local diff = {};
  local update = {};
  for ip, nodeData in pairs(gossip.networkState) do
    if utils.compareNodeData(nodeData, synData[ip]) == -1 then
      diff[ip] = nodeData;
    elseif utils.compareNodeData(nodeData, synData[ip]) == 1 then
      update[ip] = synData[ip];
    end
  end
  return update, diff
end

utils.setConfig = function(userConfig)
  for k, v in pairs(userConfig) do
    if gossip.config[k] and type(gossip.config[k]) == type(v) then
      gossip.config[k] = v;
      utils.debug('Set value for ' .. k);
    end
  end
end

-- State

state.setRev = function(revNumber)
  local revision = revNumber or 0;
  if not revNumber and file.exists(revFile) then
    revision = file.getcontents(constants.revFileName) + 1;
  end
  file.putcontents(constants.revFileName, revision);
  utils.debug('Revision set to ' .. gossip.currentState.revision);
  return revision;
end

state.setRevManually = function(revNumber)
  if revNumber then
  state.setRev(revNumber);
  utils.debug('Revision overriden to ' .. revNumber);
  else
    utils.debug('Please provide a revision number.');
  end
end

state.start = function()
  if gossip.started then
    utils.debug('Gossip already started.');
    return;
  end
  gossip.ip = wifi.sta.getip();
  if gossip.ip then
    utils.debug('Node not connected to network. Gossip will not start.');
    return;
  end
  local localState = gossip.networkState[gossip.ip];
  localState.revision = state.setRev();
  localState.heartbeat = tmr.time();
  localState.state = constants.nodeState.UP;

  gossip.inboundSocket = net.createUDPSocket();
  gossip.inboundSocket:listen(gossip.config.comPort);
  gossip.inboundSocket:on('receive', network.receiveData);

  gossip.started = true;
  gossip.timer = tmr.create();
  gossip.timer:register(gossip.config.roundInterval, tmr.ALARM_AUTO,
                        network.sendSyn);
  gossip.timer:start();
end

state.tickNodeState = function(ip)
  if gossip.networkState[ip] then
    local nodeState = gossip.networkState[ip].state;
    if nodeState < constants.nodeState.REMOVE then
      nodeState = nodeState + constants.nodeState.TICK;
      gossip.networkState[ip].state = nodeState;
    end
  end
end

-- Network

network.pushGossip = function(data, ip)
  gossip.networkState[gossip.ip].data = data;
  network.sendSyn(ip);
end

network.updateNetworkState = function(updateData)
  if gossip.updateCallback then gossip.updateCallback(updateData); end
  for ip, data in pairs(updateData) do
    if not gossip.config.seedList[ip] then
      table.insert(gossip.config.seedList, ip);
    end
    gossip.networkState[ip] = data;
  end
end

network.sendSyn = function(ip)
  local destination = ip or network.pickRandomNode();
  gossip.networkState[gossip.ip].heartbeat = tmr.time();
  if destination then
    network.sendData(randomNode, gossip.networkState, constants.updateType.SYN);
    state.tickNodeState(randomNode);
  end
end

network.pickRandomNode = function()
  if #gossip.config.seedList > 0 then
    local randomListPick = node.random(1, #gossip.config.seedList);
    utils.debug('Randomly picked: ' .. gossip.config.seedList[randomListPick]);
    return gossip.config.seedList[randomListPick];
  end
  utils.debug(
      'Seedlist is empty. Please provide one or wait for node to be contacted.');
  return nil;
end

network.sendData = function(ip, data, sendType)
  local outboundSocket = net.createUDPSocket();
  data.type = sendType;
  local dataToSend = sjson.encode(data);
  data.type = nil;
  outboundSocket:send(gossip.config.comPort, ip, dataToSend);
  outboundSocket:close();
end

network.receiveSyn = function(ip, synData)
  utils.debug('Received SYN from ' .. ip);
  local update, diff = utils.getUpdateDiffDelta(synData);
  network.updateNetworkState(update);
  network.sendAck(ip, diff);
end

network.receiveAck = function(ip, ackData)
  utils.debug('Received ACK from ' .. ip);
  local update = utils.getUpdateDiffDelta(ackData);
  utils.updateNetworkState(update);
end

network.sendAck = function(ip, diff)
  local diffIps;
  for k in pairs(diff) do diffIps = diffIps .. k; end
  utils.log('Sending ACK to ' .. ip .. ' with ' .. diffIps .. ' updates.');
  network.sendData(ip, diff, constants.updateType.ACK);
end

-- luacheck: push no unused
network.receiveData = function(socket, data, port, ip)
  if gossip.networkState[ip] then
    gossip.networkState[ip].state = constants.nodeState.UP;
  end
  local messageDecoded, updateData = pcall(sjson.decode, data);
  if not messageDecoded then
    utils.debug('Invalid JSON received from ' .. ip);
    return;
  end
  local updateType = updateData.type;
  updateData.type = nil;
  if updateType == constants.updateType.SYN then
    network.receiveSyn(ip, updateData);
  elseif updateType == constants.updateType.ACK then
    network.receiveAck(ip, updateData);
  else
    utils.debug('Invalid data comming from ip ' .. ip ..
                    '. No valid type specified.');
  end
end
-- luacheck: pop

-- Constants

constants.nodeState = {
  TICK = 1,
  UP = 0,
  SUSPECT = 2,
  DOWN = 3,
  REMOVE = 4
};

constants.defaultConfig = {
  seedList = {},
  roundInterval = 15000,
  comPort = 5000,
  debug = false
};

constants.comparisonFields = {
  'revision',
  'heartbeat',
  'state'
};

constants.updateType = {
  ACK = 'ACK',
  SYN = 'SYN'
}

constants.revFileName = 'gossip/rev.dat';

-- Return

gossip = {
  started = false,
  config = constants.defaultConfig,
  setConfig = utils.setConfig,
  start = state.start,
  setRevManually = state.setRevManually,
  networkState = {},
  getNetworkState = utils.getNetworkState,
  pushGossip = network.pushGossip
};

-- unit tests

-- uncomment this and comment the code below it to run gossip_tests
  -- return {
  --   _gossip = gossip,
  --   _constants = constants,
  --   _utils = utils,
  --   _network = network,
  --   _state = state
  -- };

if nat and file and tmr and wifi then
  return gossip;
else
  error('Gossip requires these modules to work: net, file, tmr, wifi');
end
