//========================================================================//
//================================COMMANDS================================//
//========================================================================//

String encrytptedMnemonic = "";
String passwordHash = "";

bool authenticated = false;
String command = ""; // todo: remove
String commandData = ""; // todo: remove

String message = "Welcome";
String subMessage = "Enter password";

String serialData = "";

void listenForCommands() {
  if (loadFiles() == false) {
    message = "Failed opening files";
    subMessage = "Reset or 'help'";
  }

  if (message != "" || subMessage != "")
    showMessage(message, subMessage);

  serialData = awaitSerialData();

  Command c = extractCommand(serialData);
  // flush stale data from buffer
  Serial.println("Received command: " + c.cmd);
  executeCommand(c);

  delay(DELAY_MS);
}


void executeCommand(Command c) {
  if (c.cmd == COMMAND_HELP) {
    executeHelp(c.data);
  } else if (c.cmd == COMMAND_WIPE) {
    executeWipeHww(c.data);
  } else if (c.cmd == COMMAND_PASSWORD) {
    executePasswordCheck(c.data);
  } else if (c.cmd == COMMAND_PASSWORD_CLEAR) {
    executePasswordClear(c.data);
  } else if (c.cmd == COMMAND_SEED) {
    executeShowSeed(c.data);
  } else if (c.cmd == COMMAND_SEND_PSBT) {
    executeSignPsbt(c.data);
  } else if (c.cmd == COMMAND_RESTORE) {
    executeRestore(c.data, "");
  } else {
    executeUnknown(c.data);
  }
}

void executeHelp(String commandData) {
  help();
}

void executePasswordCheck(String commandData) {
  if (commandData == "") {
    message = "Enter password";
    subMessage = "8 numbers/letters";
    return;
  }
  String hash = hashPassword(commandData);
  if (passwordHash == hash) {
    authenticated = true;
    message = "Password correct!";
    subMessage = "Ready to sign sir!";
  } else {
    authenticated = false;
    message = "Wrong password, try again";
    subMessage = "8 numbers/letters";
  }
  Serial.println(COMMAND_PASSWORD + " " + String(authenticated));
}

void executePasswordClear(String commandData) {
  authenticated = false;
  Serial.println(COMMAND_PASSWORD_CLEAR + " 1");
  showMessage("Logging out...", "");
  delay(2000);

  message = "Logged out";
  subMessage = "Enter password";
}

void executeWipeHww(String password) {
  if (password == "") {
    message = "Enter new password";
    subMessage = "8 numbers/letters";
    return;
  }

  showMessage("Resetting...", "");
  delay(2000);

  authenticated = wipeHww(password, "");
  if (authenticated == true) {
    message = "Successfully wiped!";
    subMessage = "Every new beginning comes from some other beginning's end.";
  } else {
    message = "Error, try again";
    subMessage = "8 numbers/letters";
  }
  Serial.println(COMMAND_WIPE + " " + String(authenticated));
}

void executeShowSeed(String commandData) {
  if (authenticated == false) {
    message = "Enter password!";
    subMessage = "8 numbers/letters";
    return;
  }
  message = "";
  subMessage = "";
  printMnemonic(encrytptedMnemonic);
}

void executeRestore(String mnemonic, String password) {
  if (mnemonic == "") {
    message = "Enter seed words";
    subMessage = "Separated by spaces";
    return;
  }

  int size = getMnemonicBytes(mnemonic);
  if (size == 0) {
    message = "Wrong word count!";
    subMessage = "Must be 12, 15, 18, 21 or 24";
    Serial.println(COMMAND_RESTORE + " 0");
    return;
  }

  if (!hasValidChecksum(mnemonic, size)) {
    message = "Wrong mnemonic!";
    subMessage = "Incorrect checksum";
    Serial.println(COMMAND_RESTORE + " 0");
    return;
  }

  if (password == "") {
    showMessage("Enter new password!", "8 numbers/letters");
    serialData = awaitSerialData();
    Command c = extractCommand(serialData);
    if (c.cmd != COMMAND_PASSWORD) {
      executeUnknown("");
      return;
    }
    password = c.data;
  }

  authenticated = wipeHww(password, mnemonic);
  if (authenticated == true) {
    message = "Restore successfull";
    subMessage = "/seed` to view word list";
  } else {
    message = "Error, try again";
    subMessage = "8 numbers/letters";
  }
  Serial.println(COMMAND_RESTORE + " " + String(authenticated));
}

void executeSignPsbt(String commandData) {
  if (authenticated == false) {
    message = "Enter password!";
    subMessage = "8 numbers/letters";
    return;
  }

  PSBT psbt = parseBase64Psbt(commandData);
  if (!psbt) {
    message = "Failed parsing";
    subMessage = "Send PSBT again";
    return;
  }

  HDPrivateKey hd(encrytptedMnemonic, ""); // todo: no passphrase yet
  if (!hd) { // check if it is valid
    message = "Invalid Mnemonic";
    return;
  }

  // todo: custom paths
  HDPrivateKey hd44 = hd.derive("m/44'/0'/0'"); // p2pkh
  HDPrivateKey hd49 = hd.derive("m/49'/0'/0'"); // p2sh-p2wpkh
  HDPrivateKey hd84 = hd.derive("m/84'/0'/0'"); // p2wpkh
  HDPrivateKey hd86 = hd.derive("m/86'/0'/0'"); // p2tr

  printPsbtDetails(psbt, hd44);
  Serial.println(COMMAND_SEND_PSBT);

  commandData = awaitSerialData();
  if (commandData == COMMAND_SIGN_PSBT) {
    uint8_t signed44 = psbt.sign(hd44);
    uint8_t signed49 = psbt.sign(hd49);
    uint8_t signed84 = psbt.sign(hd84);
    uint8_t signed86 = psbt.sign(hd86);
    uint8_t signedInputCount = signed44 + signed49 + signed84 + signed86;

    Serial.println(COMMAND_SIGN_PSBT + " " + psbt.toBase64());
    message = "Signed inputs:";
    // Stupid hack. For some reason `psbt.sign()` returns the square of the signed input count
    subMessage = String((int)sqrt(signedInputCount));
  } else {
    executeUnknown(commandData);
  }
}

void executeUnknown(String commandData) {
  message = "Unknown command";
  subMessage = "`/help` for details";
}


bool loadFiles() {
  FileData mnFile = readFile(SPIFFS, "/mn.txt");
  encrytptedMnemonic = mnFile.data;

  FileData pwdFile = readFile(SPIFFS, "/hash.txt");
  passwordHash = pwdFile.data;

  return mnFile.success && pwdFile.success;
}

bool wipeHww(String password, String mnemonic) {
  if (isAlphaNumeric(password) == false)
    return false;

  deleteFile(SPIFFS, "/mn.txt");
  deleteFile(SPIFFS, "/hash.txt");
  if (mnemonic == "") {
    mnemonic = createMnemonic(24); // todo: allow 12 also
  }
  passwordHash = hashPassword(password);
  writeFile(SPIFFS, "/mn.txt", mnemonic);
  writeFile(SPIFFS, "/hash.txt", passwordHash);

  delay(DELAY_MS);
  return true;
}


bool hasValidChecksum(String mnemonic, int size) {
  uint8_t out[size];
  size_t len = mnemonicToEntropy(mnemonic, out, sizeof(out));
  String deserializedMnemonic = mnemonicFromEntropy(out, sizeof(out));
  Serial.println("mnemonic: " + mnemonic);
  Serial.println("deserializedMnemonic: " + deserializedMnemonic);
  Serial.println("mnemonic == deserializedMnemonic: " + String(mnemonic == deserializedMnemonic));
  return mnemonic == deserializedMnemonic;
}


Command extractCommand(String s) {
  int spacePos = s.indexOf(" ");
  command = s.substring(0, spacePos);
  if (spacePos == -1) {
    commandData = "";
  } else {
    commandData = s.substring(spacePos + 1, s.length());
  }
  return {command, commandData};
}
