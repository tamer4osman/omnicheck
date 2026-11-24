# frozen_string_literal: true

require 'json'

# The OC011 corpus file: exactly one credential-shaped literal (an AWS
# access key id); OC006 also fires here (the file is never required).
CONFIG = {
  aws_access_key_id: 'AKIAIOSFODNN7EXAMPLE',
  region: 'us-east-1'
}.freeze
