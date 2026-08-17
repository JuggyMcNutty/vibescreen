class CalibrateShaperConfig:
    def __init__(self, config):
        self.printer = config.get_printer();

        # Last resort only. These come from [calibrate_shaper_config], which
        # nobody fills in, so in practice they are mzv at 0.0 Hz. See
        # _configured() for where the real defaults come from.
        shaper_type = config.get('shaper_type', 'mzv')
        self.shaper_type_x = config.get('shaper_type_x' , shaper_type)
        self.shaper_freq_x = config.getfloat('shaper_freq_x', 0., minval=0.)

        self.shaper_type_y = config.get('shaper_type_y' , shaper_type)
        self.shaper_freq_y = config.getfloat('shaper_freq_y', 0., minval=0.)

        # Register commands
        gcode = config.get_printer().lookup_object('gcode')
        gcode.register_command("SAVE_INPUT_SHAPER", self.cmd_save_input_shaper,
                               desc=self.cmd_SAVE_INPUT_SHAPER_help)

    def get_status(self, eventtime):
        return {}

    def _configured(self):
        """What [input_shaper] is set to right now.

        A parameter this command is not given has to fall back to what the
        printer is already running, and that is here rather than in this
        module's own section. It used to fall back to the section, so
        SAVE_INPUT_SHAPER SHAPER_FREQ_X=40.3 SHAPER_TYPE_X=mzv on its own wrote
        the Y axis as mzv at 0.0 Hz, and a frequency of zero turns that axis's
        shaping off.

        The input_shaper object itself is not usable for this: it reports an
        empty status on the Klipper the K1 ships, measured 2026-08-17.
        """
        try:
            configfile = self.printer.lookup_object('configfile')
            return configfile.get_status(None).get('settings', {}).get(
                'input_shaper', {})
        except Exception:
            # Nothing here is worth failing a save over, and the section
            # defaults still apply.
            return {}

    def cmd_save_input_shaper(self, gcmd):
        current = self._configured()

        def default(key, fallback):
            value = current.get(key)
            return fallback if value is None else value

        shaper_type = default('shaper_type', None)

        self.shaper_freq_x = gcmd.get_float(
            'SHAPER_FREQ_X', default('shaper_freq_x', self.shaper_freq_x),
            minval=0.)
        self.shaper_type_x = gcmd.get(
            'SHAPER_TYPE_X',
            default('shaper_type_x', shaper_type or self.shaper_type_x))

        self.shaper_freq_y = gcmd.get_float(
            'SHAPER_FREQ_Y', default('shaper_freq_y', self.shaper_freq_y),
            minval=0.)
        self.shaper_type_y = gcmd.get(
            'SHAPER_TYPE_Y',
            default('shaper_type_y', shaper_type or self.shaper_type_y))

        configfile = self.printer.lookup_object('configfile')

        configfile.set('input_shaper', 'shaper_type_x', self.shaper_type_x)
        configfile.set('input_shaper', 'shaper_freq_x',
                       '%.1f' % (self.shaper_freq_x,))

        configfile.set('input_shaper', 'shaper_type_y', self.shaper_type_y)
        configfile.set('input_shaper', 'shaper_freq_y',
                       '%.1f' % (self.shaper_freq_y,))

    cmd_SAVE_INPUT_SHAPER_help = (
        "Stage input shaper values for the next SAVE_CONFIG. Anything not "
        "given keeps what [input_shaper] is set to now")

def load_config(config):
    return CalibrateShaperConfig(config)
