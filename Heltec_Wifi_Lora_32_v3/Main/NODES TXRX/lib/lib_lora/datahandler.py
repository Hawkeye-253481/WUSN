import mpyaes
from .packets import *
from lib.lib_other.utils import get_node_id

class DataHandler:
    def __init__(self, key):
        if type(key) != bytes:
            raise ValueError("Key has to be a byte array")
        if len(key) != 32:
            raise ValueError("Key has to be 16 bytes long")
        self.key = key


    def encrypt(self, data):
        iv = mpyaes.generate_IV(16)
        cipher = mpyaes.new(self.key, mpyaes.MODE_CBC, iv)
        return iv + cipher.encrypt(data)

    def decrypt(self, data):
        iv = data[:16]
        cipher = mpyaes.new(self.key, mpyaes.MODE_CBC, iv)
        return cipher.decrypt(data[16:])

    def EncMsg(self, data_to_be_sent, receiver, seq : int = 0, retry_count : int = 0, ack_request : bool=True, data_type : str = "universal"):
        """
        Creates a packet and encrypts it
        data_to_be_sent: bytes
        receiver: int
        seq: int
        retry_count: int
        ack_request: bool
        """
        pkg = None
        pkg = UniversalPacket(get_node_id())
        pkg.set_params(data_to_be_sent)
        pkg.set_receiver(receiver)
        pkg.set_sequence(seq)
        pkg.set_retry_count(retry_count)
        if ack_request:
            pkg.set_ack_request()
        return self.encrypt(pkg.create_packet())

    def receiverEncPacket(self, packet, is_sniffer=False):
        data = self.decrypt(packet)
        p = BasePacket()
        p.parse_packet(data)

        if p.get_type() == BasePacket.TYPE_UNIVERSAL:
            p = UniversalPacket()
        elif p.get_type() == BasePacket.TYPE_ACK:
            p = AckPacket()

        p.parse_packet(data)

        if p.get_broadcast() or p.get_receiver() == get_node_id() or is_sniffer:
            return p
        else:
            # packet not for us
            # print("Dropping packet: Not for us")
            return None